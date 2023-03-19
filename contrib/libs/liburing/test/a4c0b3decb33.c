#include "../config-host.h"
/* SPDX-License-Identifier: MIT */
// autogenerated by syzkaller (https://github.com/google/syzkaller)

#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "liburing.h"
#include "helpers.h"
#include "../src/syscall.h"

static void sleep_ms(uint64_t ms)
{
	usleep(ms * 1000);
}

static uint64_t current_time_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		exit(1);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static bool write_file(const char* file, const char* what, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, what);
	vsnprintf(buf, sizeof(buf), what, args);
	va_end(args);
	buf[sizeof(buf) - 1] = 0;
	int len = strlen(buf);
	int fd = open(file, O_WRONLY | O_CLOEXEC);
	if (fd == -1)
		return false;
	if (write(fd, buf, len) != len) {
		int err = errno;
		close(fd);
		errno = err;
		return false;
	}
	close(fd);
	return true;
}

static void kill_and_wait(int pid, int* status)
{
	kill(-pid, SIGKILL);
	kill(pid, SIGKILL);
	int i;
	for (i = 0; i < 100; i++) {
		if (waitpid(-1, status, WNOHANG | __WALL) == pid)
			return;
		usleep(1000);
	}
	DIR* dir = opendir("/sys/fs/fuse/connections");
	if (dir) {
		for (;;) {
			struct dirent* ent = readdir(dir);
			if (!ent)
				break;
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
				continue;
			char abort[300];
			snprintf(abort, sizeof(abort), "/sys/fs/fuse/connections/%s/abort",
					ent->d_name);
			int fd = open(abort, O_WRONLY);
			if (fd == -1) {
				continue;
			}
			if (write(fd, abort, 1) < 0) {
			}
			close(fd);
		}
		closedir(dir);
	} else {
	}
	while (waitpid(-1, status, __WALL) != pid) {
	}
}

static void setup_test()
{
	prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
	setpgrp();
	write_file("/proc/self/oom_score_adj", "1000");
}

static void execute_one(void);

#define WAIT_FLAGS __WALL

static void loop(void)
{
	int iter;
	for (iter = 0; iter < 5000; iter++) {
		int pid = fork();
		if (pid < 0)
			exit(1);
		if (pid == 0) {
			setup_test();
			execute_one();
			exit(0);
		}
		int status = 0;
		uint64_t start = current_time_ms();
		for (;;) {
			if (waitpid(-1, &status, WNOHANG | WAIT_FLAGS) == pid)
				break;
			sleep_ms(1);
			if (current_time_ms() - start < 5 * 1000)
				continue;
			kill_and_wait(pid, &status);
			break;
		}
	}
}

void execute_one(void)
{
	*(uint32_t*)0x20000080 = 0;
	*(uint32_t*)0x20000084 = 0;
	*(uint32_t*)0x20000088 = 3;
	*(uint32_t*)0x2000008c = 3;
	*(uint32_t*)0x20000090 = 0x175;
	*(uint32_t*)0x20000094 = 0;
	*(uint32_t*)0x20000098 = 0;
	*(uint32_t*)0x2000009c = 0;
	*(uint32_t*)0x200000a0 = 0;
	*(uint32_t*)0x200000a4 = 0;
	*(uint32_t*)0x200000a8 = 0;
	*(uint32_t*)0x200000ac = 0;
	*(uint32_t*)0x200000b0 = 0;
	*(uint32_t*)0x200000b4 = 0;
	*(uint32_t*)0x200000b8 = 0;
	*(uint32_t*)0x200000bc = 0;
	*(uint32_t*)0x200000c0 = 0;
	*(uint32_t*)0x200000c4 = 0;
	*(uint64_t*)0x200000c8 = 0;
	*(uint32_t*)0x200000d0 = 0;
	*(uint32_t*)0x200000d4 = 0;
	*(uint32_t*)0x200000d8 = 0;
	*(uint32_t*)0x200000dc = 0;
	*(uint32_t*)0x200000e0 = 0;
	*(uint32_t*)0x200000e4 = 0;
	*(uint32_t*)0x200000e8 = 0;
	*(uint32_t*)0x200000ec = 0;
	*(uint64_t*)0x200000f0 = 0;
	__sys_io_uring_setup(0x983, (struct io_uring_params *) 0x20000080);
}

static void sig_int(int sig)
{
	exit(0);
}

int main(int argc, char *argv[])
{
	if (argc > 1)
		return T_EXIT_SKIP;
	signal(SIGINT, sig_int);
	mmap((void *) 0x20000000, 0x1000000, 3, 0x32, -1, 0);
	loop();
	return T_EXIT_PASS;
}
