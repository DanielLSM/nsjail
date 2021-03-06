/*

   nsjail - cgroup namespacing
   -----------------------------------------

   Copyright 2014 Google Inc. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#include "cgroup.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logs.h"
#include "util.h"

namespace cgroup {

static bool initNsFromParentMem(nsjconf_t* nsjconf, pid_t pid) {
	if (nsjconf->cgroup_mem_max == (size_t)0) {
		return true;
	}

	char mem_cgroup_path[PATH_MAX];
	snprintf(mem_cgroup_path, sizeof(mem_cgroup_path), "%s/%s/NSJAIL.%d",
	    nsjconf->cgroup_mem_mount.c_str(), nsjconf->cgroup_mem_parent.c_str(), (int)pid);
	LOG_D("Create '%s' for PID=%d", mem_cgroup_path, (int)pid);
	if (mkdir(mem_cgroup_path, 0700) == -1 && errno != EEXIST) {
		PLOG_W("mkdir('%s', 0700) failed", mem_cgroup_path);
		return false;
	}

	char fname[PATH_MAX];
	std::string mem_max_str = std::to_string(nsjconf->cgroup_mem_max);
	snprintf(fname, sizeof(fname), "%s/memory.limit_in_bytes", mem_cgroup_path);
	LOG_D("Setting '%s' to '%s'", fname, mem_max_str.c_str());
	if (!util::writeBufToFile(
		fname, mem_max_str.data(), mem_max_str.length(), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update memory cgroup max limit");
		return false;
	}

	/*
	 * Use OOM-killer instead of making processes hang/sleep
	 */
	snprintf(fname, sizeof(fname), "%s/memory.oom_control", mem_cgroup_path);
	LOG_D("Writting '0' '%s'", fname);
	if (!util::writeBufToFile(fname, "0", strlen("0"), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update memory cgroup oom control");
		return false;
	}

	std::string pid_str = std::to_string(pid);
	snprintf(fname, sizeof(fname), "%s/tasks", mem_cgroup_path);
	LOG_D("Adding PID='%s' to '%s'", pid_str.c_str(), fname);
	if (!util::writeBufToFile(fname, pid_str.data(), pid_str.length(), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update '%s' task list", fname);
		return false;
	}

	return true;
}

static bool initNsFromParentPids(nsjconf_t* nsjconf, pid_t pid) {
	if (nsjconf->cgroup_pids_max == 0U) {
		return true;
	}

	char pids_cgroup_path[PATH_MAX];
	snprintf(pids_cgroup_path, sizeof(pids_cgroup_path), "%s/%s/NSJAIL.%d",
	    nsjconf->cgroup_pids_mount.c_str(), nsjconf->cgroup_pids_parent.c_str(), (int)pid);
	LOG_D("Create '%s' for PID=%d", pids_cgroup_path, (int)pid);
	if (mkdir(pids_cgroup_path, 0700) == -1 && errno != EEXIST) {
		PLOG_W("mkdir('%s', 0700) failed", pids_cgroup_path);
		return false;
	}

	char fname[PATH_MAX];
	std::string pids_max_str = std::to_string(nsjconf->cgroup_pids_max);
	snprintf(fname, sizeof(fname), "%s/pids.max", pids_cgroup_path);
	LOG_D("Setting '%s' to '%s'", fname, pids_max_str.c_str());
	if (!util::writeBufToFile(
		fname, pids_max_str.data(), pids_max_str.length(), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update pids cgroup max limit");
		return false;
	}

	std::string pid_str = std::to_string(pid);
	snprintf(fname, sizeof(fname), "%s/tasks", pids_cgroup_path);
	LOG_D("Adding PID='%s' to '%s'", pid_str.c_str(), fname);
	if (!util::writeBufToFile(fname, pid_str.data(), pid_str.length(), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update '%s' task list", fname);
		return false;
	}

	return true;
}

static bool initNsFromParentNetCls(nsjconf_t* nsjconf, pid_t pid) {
	if (nsjconf->cgroup_net_cls_classid == 0U) {
		return true;
	}

	char net_cls_cgroup_path[PATH_MAX];
	snprintf(net_cls_cgroup_path, sizeof(net_cls_cgroup_path), "%s/%s/NSJAIL.%d",
	    nsjconf->cgroup_net_cls_mount.c_str(), nsjconf->cgroup_net_cls_parent.c_str(),
	    (int)pid);
	LOG_D("Create '%s' for PID=%d", net_cls_cgroup_path, (int)pid);
	if (mkdir(net_cls_cgroup_path, 0700) == -1 && errno != EEXIST) {
		PLOG_W("mkdir('%s', 0700) failed", net_cls_cgroup_path);
		return false;
	}

	char fname[PATH_MAX];
	char net_cls_classid_str[512];
	snprintf(net_cls_classid_str, sizeof(net_cls_classid_str), "0x%x",
	    nsjconf->cgroup_net_cls_classid);
	snprintf(fname, sizeof(fname), "%s/net_cls.classid", net_cls_cgroup_path);
	LOG_D("Setting '%s' to '%s'", fname, net_cls_classid_str);
	if (!util::writeBufToFile(
		fname, net_cls_classid_str, strlen(net_cls_classid_str), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update net_cls cgroup classid");
		return false;
	}

	std::string pid_str = std::to_string(pid);
	snprintf(fname, sizeof(fname), "%s/tasks", net_cls_cgroup_path);
	LOG_D("Adding PID='%s' to '%s'", pid_str.c_str(), fname);
	if (!util::writeBufToFile(fname, pid_str.data(), pid_str.length(), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update '%s' task list", fname);
		return false;
	}

	return true;
}

static bool initNsFromParentCpu(nsjconf_t* nsjconf, pid_t pid) {
	if (nsjconf->cgroup_cpu_ms_per_sec == 0U) {
		return true;
	}

	char cpu_cgroup_path[PATH_MAX];
	snprintf(cpu_cgroup_path, sizeof(cpu_cgroup_path), "%s/%s/NSJAIL.%d",
	    nsjconf->cgroup_cpu_mount.c_str(), nsjconf->cgroup_cpu_parent.c_str(), (int)pid);
	LOG_D("Create '%s' for PID=%d", cpu_cgroup_path, (int)pid);
	if (mkdir(cpu_cgroup_path, 0700) == -1 && errno != EEXIST) {
		PLOG_W("mkdir('%s', 0700) failed", cpu_cgroup_path);
		return false;
	}

	char fname[PATH_MAX];
	char cpu_ms_per_sec_str[512];
	snprintf(cpu_ms_per_sec_str, sizeof(cpu_ms_per_sec_str), "%u",
	    nsjconf->cgroup_cpu_ms_per_sec * 1000U);
	snprintf(fname, sizeof(fname), "%s/cpu.cfs_quota_us", cpu_cgroup_path);
	LOG_D("Setting '%s' to '%s'", fname, cpu_ms_per_sec_str);
	if (!util::writeBufToFile(
		fname, cpu_ms_per_sec_str, strlen(cpu_ms_per_sec_str), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update cpu quota");
		return false;
	}

	static const char cpu_period_us[] = "1000000";
	snprintf(fname, sizeof(fname), "%s/cpu.cfs_period_us", cpu_cgroup_path);
	LOG_D("Setting '%s' to '%s'", fname, cpu_period_us);
	if (!util::writeBufToFile(
		fname, cpu_period_us, strlen(cpu_period_us), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update cpu period");
		return false;
	}

	std::string pid_str = std::to_string(pid);
	snprintf(fname, sizeof(fname), "%s/tasks", cpu_cgroup_path);
	LOG_D("Adding PID='%s' to '%s'", pid_str.c_str(), fname);
	if (!util::writeBufToFile(fname, pid_str.data(), pid_str.length(), O_WRONLY | O_CLOEXEC)) {
		LOG_W("Could not update '%s' task list", fname);
		return false;
	}

	return true;
}

bool initNsFromParent(nsjconf_t* nsjconf, pid_t pid) {
	if (!initNsFromParentMem(nsjconf, pid)) {
		return false;
	}
	if (!initNsFromParentPids(nsjconf, pid)) {
		return false;
	}
	if (!initNsFromParentNetCls(nsjconf, pid)) {
		return false;
	}
	if (!initNsFromParentCpu(nsjconf, pid)) {
		return false;
	}
	return true;
}

void finishFromParentMem(nsjconf_t* nsjconf, pid_t pid) {
	if (nsjconf->cgroup_mem_max == (size_t)0) {
		return;
	}
	char mem_cgroup_path[PATH_MAX];
	snprintf(mem_cgroup_path, sizeof(mem_cgroup_path), "%s/%s/NSJAIL.%d",
	    nsjconf->cgroup_mem_mount.c_str(), nsjconf->cgroup_mem_parent.c_str(), (int)pid);
	LOG_D("Remove '%s'", mem_cgroup_path);
	if (rmdir(mem_cgroup_path) == -1) {
		PLOG_W("rmdir('%s') failed", mem_cgroup_path);
	}
	return;
}

void finishFromParentPids(nsjconf_t* nsjconf, pid_t pid) {
	if (nsjconf->cgroup_pids_max == 0U) {
		return;
	}
	char pids_cgroup_path[PATH_MAX];
	snprintf(pids_cgroup_path, sizeof(pids_cgroup_path), "%s/%s/NSJAIL.%d",
	    nsjconf->cgroup_pids_mount.c_str(), nsjconf->cgroup_pids_parent.c_str(), (int)pid);
	LOG_D("Remove '%s'", pids_cgroup_path);
	if (rmdir(pids_cgroup_path) == -1) {
		PLOG_W("rmdir('%s') failed", pids_cgroup_path);
	}
	return;
}

void finishFromParentCpu(nsjconf_t* nsjconf, pid_t pid) {
	if (nsjconf->cgroup_cpu_ms_per_sec == 0U) {
		return;
	}
	char cpu_cgroup_path[PATH_MAX];
	snprintf(cpu_cgroup_path, sizeof(cpu_cgroup_path), "%s/%s/NSJAIL.%d",
	    nsjconf->cgroup_cpu_mount.c_str(), nsjconf->cgroup_cpu_parent.c_str(), (int)pid);
	LOG_D("Remove '%s'", cpu_cgroup_path);
	if (rmdir(cpu_cgroup_path) == -1) {
		PLOG_W("rmdir('%s') failed", cpu_cgroup_path);
	}
	return;
}

void finishFromParentNetCls(nsjconf_t* nsjconf, pid_t pid) {
	if (nsjconf->cgroup_net_cls_classid == 0U) {
		return;
	}
	char net_cls_cgroup_path[PATH_MAX];
	snprintf(net_cls_cgroup_path, sizeof(net_cls_cgroup_path), "%s/%s/NSJAIL.%d",
	    nsjconf->cgroup_net_cls_mount.c_str(), nsjconf->cgroup_net_cls_parent.c_str(),
	    (int)pid);
	LOG_D("Remove '%s'", net_cls_cgroup_path);
	if (rmdir(net_cls_cgroup_path) == -1) {
		PLOG_W("rmdir('%s') failed", net_cls_cgroup_path);
	}
	return;
}

void finishFromParent(nsjconf_t* nsjconf, pid_t pid) {
	finishFromParentMem(nsjconf, pid);
	finishFromParentPids(nsjconf, pid);
	finishFromParentNetCls(nsjconf, pid);
	finishFromParentCpu(nsjconf, pid);
}

bool initNs(void) {
	return true;
}

}  // namespace cgroup
