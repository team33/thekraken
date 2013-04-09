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
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/ptrace.h>

#include <dirent.h>

#define WELCOME_LINES "thekraken: The Kraken 0.3\nthekraken: Processor affinity wrapper for Folding@Home\nthekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2\n"
#define CA5_SHORT "FahCore_a5"
#define CA3_SHORT "FahCore_a3"
#define CA5 "FahCore_a5.exe"
#define CA3 "FahCore_a3.exe"
#define SIZE_THRESH 204800
#define LOGFN "thekraken.log"
#define LOGFN_PREV "thekraken-prev.log"

static char *core_list[] = { CA3, CA3_SHORT, CA5, CA5_SHORT, NULL };

extern char **environ;
static int cpid;
static FILE *fp;

static int debug_level;
#define debug(_lev) if (debug_level >= _lev)

static void sighandler(int n)
{
	fprintf(fp, "thekraken: got signal 0x%08x\n", n);
	kill(cpid, n);
}

static int do_install(char *s)
{
	int fd1, fd2;
	char buf[8192];
	int rv, rv2;;
	
	fd1 = open("/proc/self/exe", O_RDONLY);
	fd2 = open(s, O_WRONLY|O_TRUNC|O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO);
	if (fd1 == -1 || fd2 == -1)
		goto out_err;
	while ((rv = read(fd1, buf, sizeof(buf))) > 0) {
		rv2 = write(fd2, buf, rv);
		if (rv2 != rv)
			goto out_err;
	}
	close(fd1);
	close(fd2);
	
	return 0;
out_err:
	close(fd1);
	close(fd2);
	return -1;
}

static int install(char *s)
{
	struct stat st;
	char fn[32];
	
	if (lstat(s, &st)) {
		return -1;
	}
	if (!S_ISREG(st.st_mode)) {
		return -1;
	}
	if (st.st_size < SIZE_THRESH) {
		return 1; // already installed
	}
	sprintf(fn, "thekraken-%s", s);
	if (rename(s, fn)) {
		return 2; // problems, no installation performed
	}
	if (do_install(s)) {
		rename(fn, s);
		return 3; // problems, no installation performed
	}
	return 0;
}

static void install_summary(char *s, int rv)
{
	switch (rv) {
		case -1:
			debug(1) fprintf(stderr, "thekraken: %s not found, skipping\n", s);
			break;
		case 0:
			fprintf(stderr, "thekraken: %s: wrapper succesfully installed\n", s);
			break;
		case 1:
			fprintf(stderr, "thekraken: %s: wrapper already installed, no installation performed\n", s);
			break;
		default:
			fprintf(stderr, "thekraken: %s: problems occurred during installation, no installation performed (code %d)\n", s, rv);
	}
}

static int list_install(void)
{
	int ret = 0;
	char **s = core_list;
	int rv;
	
	while (*s) {
		if (!(rv = install(*s)))
			ret++;
		install_summary(*s, rv);
		s++;
	}
	return ret;
}

static int uninstall(char *s)
{	
	char fn[32];
	struct stat st;
	int rv;
	
	sprintf(fn, "thekraken-%s", s);
	
	rv = lstat(fn, &st);
	if (rv == -1) {
		if (errno == ENOENT)
			return 1;
		return -1;
	}
	return rename(fn, s);
}

static void uninstall_summary(char *s, int rv)
{
	switch (rv) {
		case 0:
			fprintf(stderr, "thekraken: %s: wrapper succesfully uninstalled\n", s);
			break;
		case 1:
			debug(1) fprintf(stderr, "thekraken: %s: wrapper not installed; nothing to uninstall\n", s);
			break;
		default:
			fprintf(stderr, "thekraken: %s: problems occurred during uninstallation, no uninstallation performed (code %d)\n", s, rv);
	}
}

static int list_uninstall(void)
{
	int ret = 0;
	char **s = core_list;
	int rv;
	
	while (*s) {
		if (!(rv = uninstall(*s)))
			ret++;
		uninstall_summary(*s, rv);
		s++;
	}
	return ret;
}

static void traverse(char *root, char *what, int how, int options, int *counter)
{
	DIR *d;
	struct dirent *de;
	char *rroot = root;
	
	if (chdir(what)) {
		goto out;
	}
	if (root == NULL) {
		rroot = get_current_dir_name();
		if (!rroot)
			goto out;
	}
	debug(1) fprintf(stderr, "thekraken: entered %s\n", what);
	if (!how) {
		(*counter) += list_install();
	} else {
		(*counter) += list_uninstall();
	}
	d = opendir(".");
	if (!d) {
		goto out_up;
	}
	while ((de = readdir(d))) {
		if (!strcmp(de->d_name, "."))
			continue;
		if (!strcmp(de->d_name, ".."))
			continue;
		if (!strcmp(de->d_name, "work"))
			continue;
		traverse(rroot, de->d_name, how, options, counter);
	}
	closedir(d);
out_up:
	debug(1) fprintf(stderr, "thekraken: leaving %s\n", what);
	if (root != NULL) {
		char * s;

		chdir("..");
		s = get_current_dir_name();
		debug(2) fprintf(stderr, "thekraken: root: %s, now: %s\n", root, s);
		free(s);
	} else {
		free(rroot);
	}
out:
	return;
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
	
	if (strstr(av[0], "thekraken")) {
		int c; 
		int opt_install = 0, opt_uninstall = 0, opt_help = 0, opt_yes = 0;
		char *path = NULL;
		int counter = 0;
		
		fprintf(stderr, WELCOME_LINES);
		opterr = 0;
		while ((c = getopt(ac, av, "+iuhyv")) != -1) {
			switch (c) {
				case 'i':
					opt_install = 1;
					break;
				case 'u':
					opt_uninstall = 1;
					break;
				case 'h':
					opt_help = 1;
					break;
				case 'y':
					opt_yes = 1;
					break;
				case 'v':
					debug_level++;
					break;
				case '?':
					fprintf(stderr, "thekraken: error: option not recognized: -%c\n", optopt);
					return -1;
				default:
					fprintf(stderr, "thekraken: internal error (1); please report this issue\n");
					return -1;
			}
		}
		if (opt_install || opt_uninstall) {
			if (optind + 1 < ac) {
				fprintf(stderr, "thekraken: error: parameter not recognized: %s\n", av[optind + 1]);
				return -1;
			}
		} else {
			if (optind == ac) {
				opt_help = 1;
			} else {
				fprintf(stderr, "thekraken: error: parameter not recognized: %s\n", av[optind]);
				return -1;
			}
		}
		if (opt_help == 1) {
			fprintf(stderr, "Usage:\n");
			fprintf(stderr, "\t%s [-y] -i [path]\n", av[0]);
			fprintf(stderr, "\t%s [-y] -u [path]\n", av[0]);
			fprintf(stderr, "\t%s -h\n", av[0]);
			fprintf(stderr, "\n");
			fprintf(stderr, "Options:\n");
			fprintf(stderr, "\t-i [path]\tinstall The Kraken in 'path' directory and all its\n");
			fprintf(stderr, "\t\t\tsubdirectories; if path is omitted, current directory\n");
			fprintf(stderr, "\t\t\tis used\n");
			fprintf(stderr, "\t-u [path]\tuninstall The Kraken from 'path' directory and all its\n");
			fprintf(stderr, "\t\t\tsubdirectories; if path is omitted, current directory\n");
			fprintf(stderr, "\t\t\tis used\n");
			fprintf(stderr, "\t-v\t\tincrease verbosity; can be specified multiple times\n");
			fprintf(stderr, "\t-y\t\tdo not ask for confirmation (non-interactive mode)\n");
			fprintf(stderr, "\t-h\t\tdisplay this help and exit\n");
			return 0;
		}
		if (opt_install && opt_uninstall) {
			fprintf(stderr, "thekraken: error: can't install and uninstall at the same time; choose one\n");
			return -1;
		}
		if (optind < ac) {
			path = av[optind];
		}
		if (path == NULL) {
			path = ".";
		}
		if (opt_install) {
			fprintf(stderr, "thekraken: performing installation to %s\n", path);
			traverse(NULL, path, 0, opt_yes, &counter);
#if 0
			rv = install(CA5);
			install_summary(CA5, rv);
			rv = install(CA3);
			install_summary(CA3, rv);
#endif
			fprintf(stderr, "thekraken: finished installation, %d files processed\n", counter);
			return 0;
		}
		if (opt_uninstall) {
			fprintf(stderr, "thekraken: performing uninstallation from %s\n", path);
			traverse(NULL, path, 1, opt_yes, &counter);
#if 0
			rv = uninstall(CA5);
			uninstall_summary(CA5, rv);
			rv = uninstall(CA3);
			uninstall_summary(CA3, rv);
#endif
			fprintf(stderr, "thekraken: finished uninstallation, %d files processed\n", counter);
			return 0;
		}

		/* just in case someone breaks the code*/
		fprintf(stderr, "thekraken: internal error (3); please report this issue\n");

		return -1;
	}

	rename(LOGFN, LOGFN_PREV);
	fp = fopen(LOGFN, "w");
	if (fp) {
		setvbuf(fp, NULL, _IONBF, 0);
		fprintf(stderr, WELCOME_LINES);
		fprintf(stderr, "thekraken: Logging to " LOGFN "\n");
	} else {
		/* fail silently */
		fp = stderr;
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
	snprintf(t, sizeof(nbin) - len - 1, "thekraken-%s", s);
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
