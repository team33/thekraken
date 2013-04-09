/*
 * Copyright (C) 2011 by Kris Rusocki <kszysiu@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <stdlib.h>
#include <sys/user.h>
#include <sched.h>

#define WELCOME_LINES "thekraken: The Kraken 0.1\nthekraken: Processor affinity wrapper for Folding@Home\nthekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2\nthekraken: Logging to thekraken.log\n"

extern char **environ;
static int cpid;
static FILE *fp;

static void sighandler(int n)
{
	fprintf(fp, "thekraken: got signal 0x%08x\n", n);
	kill(cpid, n);
}

int main(int ac, char **av)
{
	int rv;

	char nbin[256];
	char *s, *t = nbin;
	int len = 0;
	
	int status;

	int nclones = 0;
	int lastcpu = 0;
	cpu_set_t cpuset;

	
	fp = fopen("thekraken.log", "w");
	if (fp) {
		setvbuf(fp, NULL, _IONBF, 0);
		fprintf(stderr, WELCOME_LINES);
	}
	fprintf(fp, WELCOME_LINES);
	
	s = strrchr(av[0], '/');
	if (s) {
		len = s - av[0] + 1;
		memcpy(t, av[0], len);
		s++;
	} else {
		s = av[0];
	}
	t += len;
	nbin[sizeof(nbin) - 1] = '\0';
	snprintf(t, sizeof(nbin) - len - 1, "darkswarm.org-%s", s);
	fprintf(fp, "thekraken: Launch binary: %s\n", nbin);

	signal(SIGHUP, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGCHLD, SIG_IGN);
	
	cpid = fork();
	if (cpid == -1) {
		fprintf(fp, "thekraken: fork: %s\n", strerror(errno));
		return -1;
	}
	if (cpid == 0) {
		long prv;
		
		prv = ptrace(PTRACE_TRACEME, 0, 0, 0);
		fprintf(fp, "thekraken: child: ptrace(PTRACE_TRACEME) returns with %ld (%s)\n", prv, strerror(errno));
		if (prv != 0) {
			return -1;
		}
		fprintf(fp, "thekraken: child: Executing...\n");
		execvp(nbin, av);
		fprintf(fp, "thekraken: child: exec: %s\n", strerror(errno));
		return -1;
	}
		
	fprintf(fp, "thekraken: Forked %d.\n", cpid);

	while (1) {
		status = 0;
		rv = waitpid(-1, &status, __WALL);
		fprintf(fp, "thekraken: waitpid() returns %d with status 0x%08x (errno %d)\n", rv, status, errno);
		if (rv == -1) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (WIFEXITED(status)) {
			fprintf(fp, "thekraken: %d: exited with %d\n", rv, WEXITSTATUS(status));
			if (rv != cpid) {
				fprintf(fp, "thekraken: %d: ignoring clone exit\n", rv);
				continue;
			}
			return WEXITSTATUS(status);
		}
		if (WIFSIGNALED(status)) {
			/* will never happen? */
			fprintf(fp, "thekraken: %d: got signal %d\n", rv, WTERMSIG(status));
			raise(WTERMSIG(status));
			return -1;
		}
		if (WIFSTOPPED(status)) {
			int e;
			long prv;
			//struct user_regs_struct regs; // stack examination?

			fprintf(fp, "thekraken: %d: stopped with signal 0x%08x\n", rv, WSTOPSIG(status));
			if (WSTOPSIG(status) == SIGTRAP) {
				long cloned = -1;
				e = status >> 16;
				if (e & PTRACE_EVENT_CLONE) {
					int c;

					prv = ptrace(PTRACE_GETEVENTMSG, rv, 0, &cloned);
					c = cloned;
					fprintf(fp, "thekraken: %d: cloned %d\n", rv, c);
					nclones++;
					if (nclones != 2 && nclones != 3) {
						fprintf(fp, "thekraken: %d: binding to cpu %d\n", c, lastcpu);
						CPU_ZERO(&cpuset);
						CPU_SET(lastcpu, &cpuset);
						lastcpu++;
						sched_setaffinity(c, sizeof(cpuset), &cpuset);
					}
				} else if (e == 0) {
					/* initial attach? */
					fprintf(fp, "thekraken: %d: initial attach\n", rv);
					prv = ptrace(PTRACE_SETOPTIONS, rv, 0, PTRACE_O_TRACECLONE);
				}
				fprintf(fp, "thekraken: %d: Continuing.\n", rv);
				prv = ptrace(PTRACE_CONT, rv, 0, 0);
				continue;
			}
			if (WSTOPSIG(status) == SIGSTOP) {
				fprintf(fp, "thekraken: %d: Continuing.\n", rv);
				prv = ptrace(PTRACE_CONT, rv, 0, 0);
				continue;
			}
			fprintf(fp, "thekraken: %d: Continuing (forwarding signal %d).\n", rv, WSTOPSIG(status));
			prv = ptrace(PTRACE_CONT, rv, 0, WSTOPSIG(status));
			continue;
		}
		fprintf(fp, "thekraken: %d: unknown waitpid status, halt!\n", rv);
	}
	return 0;
}
