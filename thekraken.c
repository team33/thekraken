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
#include <sys/fsuid.h>
#include <termios.h>

#include "version.h"
#include "build.h"

#define WELCOME_LINES "thekraken: The Kraken " VERSION " %s\nthekraken: Processor affinity wrapper for Folding@Home\nthekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2\n"
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

static char wdbuf[PATH_MAX];
static char *k_getwd(void)
{
	return getcwd(wdbuf, sizeof(wdbuf));
}

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
		return 1; /* already installed */
	}
	sprintf(fn, "thekraken-%s", s);
	if (rename(s, fn)) {
		return 2; /* problems, no installation performed */
	}
	setfsuid(st.st_uid); /* reasonable assumption: files and directory they reside in are owned by the same user */
	setfsgid(st.st_gid);
	if (do_install(s)) {
		setfsuid(geteuid());
		setfsgid(getegid());
		rename(fn, s);
		return 3; /* problems, no installation performed */
	}
	setfsuid(geteuid());
	setfsgid(getegid());
	return 0;
}

static void install_summary(char *d, char *s, int rv)
{
	switch (rv) {
		case -1:
			debug(1) fprintf(stderr, "thekraken: %s/%s not found, skipping\n", d, s);
			break;
		case 0:
			fprintf(stderr, "thekraken: %s/%s: wrapper succesfully installed\n", d, s);
			break;
		case 1:
			fprintf(stderr, "thekraken: %s/%s: wrapper already installed, no installation performed\n", d, s);
			break;
		default:
			fprintf(stderr, "thekraken: %s/%s: problems occurred during installation, no installation performed (code %d)\n", d, s, rv);
	}
}

static void list_install(int *counter, int *total)
{
	char **s = core_list;
	int rv;
	char *d = k_getwd();
	
	while (*s) {
		if (!(rv = install(*s)))
			(*counter)++;
		if (rv >= 0) {
			(*total)++;
		}
		install_summary(d, *s, rv);
		s++;
	}
}

static int uninstall(char *s)
{	
	char fn[32];
	int rv;
	
	sprintf(fn, "thekraken-%s", s);
	
	rv = rename(fn, s);
	if (rv == -1 && errno != ENOENT) {
		rv = 1;
	}
	return rv;
}

static void uninstall_summary(char *d, char *s, int rv)
{
	switch (rv) {
		case -1:
			debug(1) fprintf(stderr, "thekraken: %s/%s: wrapper not installed; nothing to uninstall\n", d, s);
			break;
		case 0:
			fprintf(stderr, "thekraken: %s/%s: wrapper succesfully uninstalled\n", d, s);
			break;
		default:
			fprintf(stderr, "thekraken: %s/%s: problems occurred during uninstallation, no uninstallation performed (code %d)\n", d, s, rv);
	}
}

static void list_uninstall(int *counter, int *total)
{
	char **s = core_list;
	int rv;
	char *d = k_getwd();
	
	while (*s) {
		if (!(rv = uninstall(*s)))
			(*counter)++;
		if (rv >= 0) {
			(*total)++;
		}
		uninstall_summary(d, *s, rv);
		s++;
	}
}



static void traverse(char *what, int how, int options, int *counter, int *total)
{
	DIR *d;
	struct dirent *de;
	struct stat st;
	
	if (chdir(what)) {
		goto out;
	}
	debug(1) fprintf(stderr, "thekraken: entered %s\n", what);
	if (!how) {
		list_install(counter, total);
	} else {
		list_uninstall(counter, total);
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
		if (lstat(de->d_name, &st))
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;
		if (!options) {
			struct termios t;
			char *wd = k_getwd();
			int c;

			if (!tcgetattr(0, &t)) {
				fprintf(stderr, "thekraken: descend into %s/%s and all other subdirectories [Y/n]? ", wd, de->d_name);
				if ((c = getchar()) == 'y' || c == 'Y' || c == '\n')
					options = 1;
			} else {
				fprintf(stderr, "thekraken: standard input is not a terminal, not descending into %s/%s or any other subdirectories\n", wd, de->d_name);
			}
			if (!options)
				break;
		}
		if (options)
			traverse(de->d_name, how, options, counter, total);
	}
	closedir(d);
out_up:
	debug(1) fprintf(stderr, "thekraken: leaving %s\n", what);
	chdir("..");
out:
	return;
}

int main(int ac, char **av)
{
	int rv;

	char nbin[PATH_MAX];
	char *s, *t = nbin;
	int len = 0;
	
	int status;

	int nclones = 0;
	int lastcpu = 0;
	cpu_set_t cpuset;
	
	s = strrchr(av[0], '/');
	if (!s) {
		s = av[0];
	} else {
		s += 1;
	}
	
	if (strstr(s, "thekraken")) {
		int c; 
		int opt_install = 0, opt_uninstall = 0, opt_help = 0, opt_yes = 0, opt_version = 0;
		char *path = NULL;
		int counter = 0, total = 0;
		
		fprintf(stderr, WELCOME_LINES, get_build_info());
		opterr = 0;
		while ((c = getopt(ac, av, "+iuhyvV")) != -1) {
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
				case 'V':
					opt_version = 1;
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
		if (opt_version == 1) {
			return 0;
		}
		if (opt_help == 1) {
			fprintf(stderr, "Usage:\n");
			fprintf(stderr, "\t%s [-v] [-y] -i [path]\n", av[0]);
			fprintf(stderr, "\t%s [-v] [-y] -u [path]\n", av[0]);
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
			fprintf(stderr, "\t-V\t\tprint version information and exit\n");
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
			traverse(path, 0, opt_yes, &counter, &total);
			if (total == 0) {
				fprintf(stderr, "thekraken: finished installation, found no files to process\n");
			} else {
				fprintf(stderr, "thekraken: finished installation, %d out of %d files processed\n", counter, total);
			}
			return 0;
		}
		if (opt_uninstall) {
			fprintf(stderr, "thekraken: performing uninstallation from %s\n", path);
			traverse(path, 1, opt_yes, &counter, &total);
			if (total == 0) {
				fprintf(stderr, "thekraken: finished uninstallation, found no files to process\n");
			} else {
				fprintf(stderr, "thekraken: finished uninstallation, %d out of %d file(s) processed\n", counter, total);
			}
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
		fprintf(stderr, WELCOME_LINES, get_build_info());
		fprintf(stderr, "thekraken: Logging to " LOGFN "\n");
	} else {
		/* fail silently */
		fp = stderr;
	}
	fprintf(fp, WELCOME_LINES, get_build_info());
	
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
