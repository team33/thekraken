/*
 * Copyright (C) 2011,2012 by Kris Rusocki <kszysiu@gmail.com>
 * Copyright (C) 2012 by Stephen Gordon <firedfly@gmail.com>
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
#include <sys/syscall.h>
#include <sys/user.h>

#include <dirent.h>
#include <sys/fsuid.h>

#include <ctype.h>

#include <time.h>

#include "version.h"
#include "build.h"
#include "synthload.h"

#define WELCOME_LINES "thekraken: The Kraken " VERSION " %s\nthekraken: Processor affinity wrapper for Folding@Home\nthekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2\n"
#define CA5_SHORT "FahCore_a5"
#define CA3_SHORT "FahCore_a3"
#define CA5 "FahCore_a5.exe"
#define CA3 "FahCore_a3.exe"
#define SIZE_THRESH 204800
#define LOGFN "thekraken.log"
#define LOGFN_PREV "thekraken-prev.log"
#define INSTALL_FMT "thekraken-%s"

#define CONF_WARNING "#\n# WARNING: DO NOT MODIFY THIS FILE\n# Instead, uninstall The Kraken and re-install with desired configuration variables.\n#\n"
#define CONF_FN "thekraken.cfg"

static char *core_list[] = { CA3, CA3_SHORT, CA5, CA5_SHORT, NULL };

extern char **environ;

static FILE *logfp;
static int logfd;

static int debug_level;
#define debug(_lev) if (debug_level >= _lev)

static pid_t cpid; /* FahCore PID */

static int custom_config;

static char wdbuf[PATH_MAX];
static char *k_getwd(void)
{
	return getcwd(wdbuf, sizeof(wdbuf));
}

#define STR_BUF_SIZE 144
static void sighandler(int n)
{
	char buf[STR_BUF_SIZE];

	snprintf(buf, sizeof(buf), "thekraken: %d: (sighandler) got signal 0x%08x\n", getpid(), n);
	write(logfd, buf, strlen(buf));
	kill(cpid, n);
}

static void sigalrmhandler(int n)
{
	char buf[STR_BUF_SIZE];

	snprintf(buf, sizeof(buf), "thekraken: %d: (sigalrmhandler) reached startup deadline, killing FahCore\n", getpid());
	write(logfd, buf, strlen(buf));
	kill(cpid, SIGKILL);
}

static int do_install(char *s)
{
	int fd1, fd2;
	char buf[8192];
	int rv, rv2;;
	
	fd1 = open("/proc/self/exe", O_RDONLY);
	fd2 = open(s, O_WRONLY | O_TRUNC | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
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

#define OPT_YES 1
#define OPT_NOMODIFY 2

static int install(char *s, int options)
{
	struct stat st;
	char fn[32];
	
	if (stat(s, &st)) {
		return -1;
	}
	if (!S_ISREG(st.st_mode)) {
		return -1;
	}
	if (st.st_size < SIZE_THRESH) {
		return 1; /* already installed */
	}
	if (options & OPT_NOMODIFY) {
		return 0;
	}
	sprintf(fn, INSTALL_FMT, s);
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

static int list_install(int options, int *counter, int *total)
{
	char **s = core_list;
	int rv;
	char *d = k_getwd();
	int ret = 0;
	
	while (*s) {
		if (!(rv = install(*s, options))) {
			(*counter)++;
			ret = 1;
		}
		if (rv >= 0) {
			(*total)++;
		}
		install_summary(d, *s, rv);
		s++;
	}
	return ret;
}

static int uninstall(char *s, int options)
{
	char fn[32];
	int rv;
	struct stat st;
	
	sprintf(fn, INSTALL_FMT, s);

	if (stat(fn, &st)) {
		return -1;
	}
	if (!S_ISREG(st.st_mode)) {
		return 1;
	}
	if (options & OPT_NOMODIFY) {
		return 0;
	}

	rv = rename(fn, s);
	if (rv == -1) {
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

static int list_uninstall(int options, int *counter, int *total)
{
	char **s = core_list;
	int rv;
	char *d = k_getwd();
	int ret = 0;
	
	while (*s) {
		if (!(rv = uninstall(*s, options))) {
			(*counter)++;
			ret = 1;
		}
		if (rv >= 0) {
			(*total)++;
		}
		uninstall_summary(d, *s, rv);
		s++;
	}
	return ret;
}


#define CONF_STARTCPU 0 /* bind FahCore threads starting with this cpu */
#define CONF_DLBLOAD 1
#define CONF_DLBLOAD_ONPERIOD 2
#define CONF_DLBLOAD_OFFPERIOD 3
#define CONF_DLBLOAD_DEADLINE 4
#define CONF_STARTUP_DEADLINE 5
#define CONF_V 6
#define CONF_MAX 7

#define DEFAULT_STARTCPU 0
#define DEFAULT_DLBLOAD 1
#define DEFAULT_DLBLOAD_ONPERIOD 8000
#define DEFAULT_DLBLOAD_OFFPERIOD 200
#define DEFAULT_DLBLOAD_DEADLINE 300000 /* 5 minutes */
#define DEFAULT_STARTUP_DEADLINE 300 /* 5 minutes */
#define DEFAULT_V 0

static char **conf_line;
static int conf_index;
static int conf_total;
static int conf_step = 4;

static char *conf_key[] = { "startcpu", "dlbload", "dlbload_onperiod", "dlbload_offperiod", "dlbload_deadline", "startup_deadline", "v", NULL };
static char *conf_val[sizeof(conf_key)/sizeof(char *)];

static unsigned int conf_startcpu = DEFAULT_STARTCPU;
static unsigned int conf_dlbload = DEFAULT_DLBLOAD;
static unsigned int conf_dlbload_onperiod = DEFAULT_DLBLOAD_ONPERIOD;
static unsigned int conf_dlbload_offperiod = DEFAULT_DLBLOAD_OFFPERIOD;
static unsigned int conf_dlbload_deadline = DEFAULT_DLBLOAD_DEADLINE;
static unsigned int conf_startup_deadline = DEFAULT_STARTUP_DEADLINE;
static unsigned int conf_v = DEFAULT_V;

static void conf_line_add(char *s)
{
	if (conf_index == conf_total) {
		conf_total += conf_step;
		conf_step <<= 1;
		conf_line = realloc(conf_line, conf_total * sizeof(*conf_line));
	}
	conf_line[conf_index++] = s;
}

static int conf_validate_one(int n)
{
	int ret = 0;

	if (n == CONF_STARTCPU && conf_val[CONF_STARTCPU]) {
		char *end;
		
		conf_startcpu = strtol(conf_val[CONF_STARTCPU], &end, 10);
		if (*end != '\0' || conf_startcpu > 128) {
			fprintf(logfp, "thekraken: configuration variable '%s': invalid value: '%s'\n", conf_key[CONF_STARTCPU], conf_val[CONF_STARTCPU]);
			ret = 1;
			conf_startcpu = DEFAULT_STARTCPU;
		} else {
			fprintf(logfp, "thekraken: config: %s=%d\n", conf_key[CONF_STARTCPU], conf_startcpu);
		}
		return ret;
	}
	if (n == CONF_DLBLOAD && conf_val[CONF_DLBLOAD]) {
		char *end;
		
		conf_dlbload = strtol(conf_val[CONF_DLBLOAD], &end, 10);
		if (*end != '\0' || conf_dlbload > 1) {
			fprintf(logfp, "thekraken: configuration variable '%s': invalid value: '%s'\n", conf_key[CONF_DLBLOAD], conf_val[CONF_DLBLOAD]);
			ret = 1;
			conf_dlbload = DEFAULT_DLBLOAD;
		} else {
			fprintf(logfp, "thekraken: config: %s=%d\n", conf_key[CONF_DLBLOAD], conf_dlbload);
		}
		return ret;
	}
	if (n == CONF_DLBLOAD_ONPERIOD && conf_val[CONF_DLBLOAD_ONPERIOD]) {
		char *end;
		
		conf_dlbload_onperiod = strtol(conf_val[CONF_DLBLOAD_ONPERIOD], &end, 10);
		if (*end != '\0' || conf_dlbload_onperiod == 0) {
			fprintf(logfp, "thekraken: configuration variable '%s': invalid value: '%s'\n", conf_key[CONF_DLBLOAD_ONPERIOD], conf_val[CONF_DLBLOAD_ONPERIOD]);
			ret = 1;
			conf_dlbload_onperiod = DEFAULT_DLBLOAD_ONPERIOD;
		} else {
			fprintf(logfp, "thekraken: config: %s=%d\n", conf_key[CONF_DLBLOAD_ONPERIOD], conf_dlbload_onperiod);
		}
		return ret;
	}
	if (n == CONF_DLBLOAD_OFFPERIOD && conf_val[CONF_DLBLOAD_OFFPERIOD]) {
		char *end;
		
		conf_dlbload_offperiod = strtol(conf_val[CONF_DLBLOAD_OFFPERIOD], &end, 10);
		if (*end != '\0' || conf_dlbload_offperiod == 0) {
			fprintf(logfp, "thekraken: configuration variable '%s': invalid value: '%s'\n", conf_key[CONF_DLBLOAD_OFFPERIOD], conf_val[CONF_DLBLOAD_OFFPERIOD]);
			ret = 1;
			conf_dlbload_offperiod = DEFAULT_DLBLOAD_OFFPERIOD;
		} else {
			fprintf(logfp, "thekraken: config: %s=%d\n", conf_key[CONF_DLBLOAD_OFFPERIOD], conf_dlbload_offperiod);
		}
		return ret;
	}
	if (n == CONF_DLBLOAD_DEADLINE && conf_val[CONF_DLBLOAD_DEADLINE]) {
		char *end;
		
		conf_dlbload_deadline = strtol(conf_val[CONF_DLBLOAD_DEADLINE], &end, 10);
		if (*end != '\0' || conf_dlbload_deadline == 0) {
			fprintf(logfp, "thekraken: configuration variable '%s': invalid value: '%s'\n", conf_key[CONF_DLBLOAD_DEADLINE], conf_val[CONF_DLBLOAD_DEADLINE]);
			ret = 1;
			conf_dlbload_deadline = DEFAULT_DLBLOAD_DEADLINE;
		} else {
			fprintf(logfp, "thekraken: config: %s=%d\n", conf_key[CONF_DLBLOAD_DEADLINE], conf_dlbload_deadline);
		}
		return ret;
	}
	if (n == CONF_STARTUP_DEADLINE && conf_val[CONF_STARTUP_DEADLINE]) {
		char *end;
		
		conf_startup_deadline = strtol(conf_val[CONF_STARTUP_DEADLINE], &end, 10);
		if (*end != '\0') {
			fprintf(logfp, "thekraken: configuration variable '%s': invalid value: '%s'\n", conf_key[CONF_STARTUP_DEADLINE], conf_val[CONF_STARTUP_DEADLINE]);
			ret = 1;
			conf_startup_deadline = DEFAULT_STARTUP_DEADLINE;
		} else {
			fprintf(logfp, "thekraken: config: %s=%d\n", conf_key[CONF_STARTUP_DEADLINE], conf_startup_deadline);
		}
		return ret;
	}
	if (n == CONF_V && conf_val[CONF_V]) {
		char *end;
		
		conf_v = strtol(conf_val[CONF_V], &end, 10);
		if (*end != '\0') {
			fprintf(logfp, "thekraken: configuration variable '%s': invalid value: '%s'\n", conf_key[CONF_V], conf_val[CONF_V]);
			ret = 1;
			conf_v = DEFAULT_V;
		} else {
			fprintf(logfp, "thekraken: config: %s=%d\n", conf_key[CONF_V], conf_v);
		}
		return ret;
	}

	return 2;
}

/* TODO: fscanf? */
static int conf_line_parse(char *s)
{
	char *e;
	char key[32];
	int i;
	int klen;
	
	e = strchr(s, '=');
	if (!e) {
		return -1;
	}
	klen = e - s;
	if (klen >= sizeof(key)) {
		return -2;
	}
	memcpy(key, s, klen);
	key[klen] = '\0';

	for (i = 0; conf_key[i]; i++) {
		int len, vlen;
		
		len = strlen(s);
		if (!isprint(s[len - 1])) {
			len--;
		}
		vlen = len - (klen + 1);
		if (!strcmp(conf_key[i], key)) {
			if (conf_val[i]) {
				fprintf(logfp, "thekraken: WARNING: configuration variable '%s' defined multiple times; last definition in effect\n", key);
				free(conf_val[i]);
			}
			conf_val[i] = malloc(vlen + 1);
			memcpy(conf_val[i], e + 1, vlen);
			conf_val[i][vlen] = '\0';
			if (conf_validate_one(i))
				return -4;
			return 0;
		}
	}
	return -3;
}

static int conf_file_parse(char *fn)
{
	FILE *fp;
	char buf[128];
	
	fp = fopen(fn, "r");
	if (!fp) {
		return 1;
	}
	while (fgets(buf, sizeof(buf), fp)) {
		buf[sizeof(buf) - 1] = '\0';
		conf_line_parse(buf);
	}
	fclose(fp);
	
	return 0;
}

static void conf_create(int options)
{
	FILE *fp;
	struct stat st;
	
	if (stat(".", &st)) {
		return;
	}
	if (options & OPT_NOMODIFY) {
		return;
	}
	setfsuid(st.st_uid); /* reasonable assumption: files, and directory they reside in are owned by the same user */
	setfsgid(st.st_gid);
	fp = fopen(CONF_FN, "w");
	if (fp) {
		int i;

		fprintf(fp, CONF_WARNING);
		for (i = 0; i < conf_index; i++) {
			fprintf(fp, "%s\n", conf_line[i]);
		}
		fclose(fp);
	}
	setfsuid(geteuid());
	setfsgid(getegid());
}

static void traverse(char *what, int how, int options, int *counter, int *total)
{
	DIR *d;
	struct dirent *de;
	struct stat st;
	int answered = 0;
	
	if (chdir(what)) {
		goto out;
	}
	debug(1) fprintf(stderr, "thekraken: entered %s\n", what);
	if (!how) {
		if (list_install(options, counter, total)) {
			if (custom_config) {
				/* some installations performed, create config file */
				fprintf(stderr, "thekraken: creating configuration file\n");
				conf_create(options);
			}
		}
	} else {
		if (list_uninstall(options, counter, total)) {
			if (!stat(CONF_FN, &st)) {
				/* some uninstallations performed, remove config file */
				fprintf(stderr, "thekraken: removing configuration file\n");
				if ((options & OPT_NOMODIFY) == 0) {
					unlink(CONF_FN);
				}
			}
		}
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
		if (stat(de->d_name, &st))
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;
		if (!answered && (options & OPT_YES) == 0) {
			char *wd = k_getwd();
			int c;

			if (isatty(0)) {
				fprintf(stderr, "thekraken: descend into %s/%s and all other subdirectories [Y/n]? ", wd, de->d_name);
				if ((c = getchar()) == 'y' || c == 'Y' || c == '\n')
					options |= OPT_YES;
			} else {
				fprintf(stderr, "thekraken: standard input is not a terminal, not descending into %s/%s or any other subdirectories\n", wd, de->d_name);
			}
			answered = 1;
		}
		if (options & OPT_YES)
			traverse(de->d_name, how, options, counter, total);
	}
	closedir(d);
out_up:
	debug(1) fprintf(stderr, "thekraken: leaving %s\n", what);
	chdir("..");
out:
	return;
}

static void getstr(pid_t child, long addr, int len, char *dst, int *dstofs, int dstsize)
{
	int i = 0;
	char *curstr = dst + *dstofs;
	long chunk;
	int plen;
	
	if (len > dstsize - *dstofs - 1 || len == -1) {
		plen = dstsize - *dstofs - 1;
	} else {
		plen = len;
	}

	while (i < plen) {
		int tocpy = plen - i > sizeof(long) ? sizeof(long) : plen - i;

		chunk = ptrace(PTRACE_PEEKDATA, child, addr + i);
		memcpy(curstr, &chunk, tocpy);
		i += tocpy;
		curstr += tocpy;

		if (len == -1) {
			while (tocpy > 0) {
				if ((chunk & 0xFF) == 0)
					break;
				chunk >>= 8;
				tocpy--;
			}
		}
	}
	*curstr = '\0';
	*dstofs = curstr - dst;
}

int main(int ac, char **av)
{
	char nbin[PATH_MAX];
	char *s, *t = nbin;
	int len = 0;
	char config[PATH_MAX];
	char *u = config;
	
	int status;

	int nclones = -1;
	int last_used_cpu = 0;
	cpu_set_t cpuset;

	pid_t tpid = 0; /* traced (syscall) thread PID */
	pid_t mpid = 0; /* load manager PID */

#define FAHCORE_BUF_SIZE 128
	int fahcore_logfd = -1;

	/* setup the buffer to store data written to logfile_xx.txt and stderr */
	char fahcore_logbuf[FAHCORE_BUF_SIZE];
	int fahcore_logbufpos = 0;
	char fahcore_errbuf[FAHCORE_BUF_SIZE];
	int fahcore_errbufpos = 0;

	int tpid_insyscall = 0;
	int cpid_insyscall = 0;
	
	time_t synthload_start_time = 0;
	
	int shutdown = 0;

	s = strrchr(av[0], '/');
	if (!s) {
		s = av[0];
	} else {
		s += 1;
	}
	
	if (strstr(s, "thekraken")) {
		int c; 
		int opt_install = 0, opt_uninstall = 0, opt_help = 0, opt_yes = 0, opt_version = 0, opt_nomodify = 0;
		char *path = NULL;
		int counter = 0, total = 0;
		int rv;

		/* ugly, for conf_line_parse() */
		logfp = stderr;
		logfd = 2;

		fprintf(stderr, WELCOME_LINES, get_build_info());
		opterr = 0;
		while ((c = getopt(ac, av, "+iuhyvnVc:")) != -1) {
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
				case 'n':
					opt_nomodify = 1;
					break;
				case 'c':
					custom_config = 1;
					conf_line_add(av[optind - 1]);
					rv = conf_line_parse(av[optind -1]);
					switch (rv) {
						case -1:
						case -2:
							fprintf(stderr, "thekraken: invalid configuration variable: '%s'\n", av[optind - 1]);
							break;
						case -3:
							fprintf(stderr, "thekraken: unknown configuration variable in '%s'\n", av[optind - 1]);
							break;
					}
					if (rv < 0)
						return -1;
					break;
				case '?':
					if (optopt != 'c')
						fprintf(stderr, "thekraken: ERROR: option not recognized: -%c\n", optopt);
					else
						fprintf(stderr, "thekraken: ERROR: option '-%c' requires an argument\n", optopt);
					return -1;
				default:
					fprintf(stderr, "thekraken: internal error (1); please report this issue\n");
					return -1;
			}
		}
		rv = opt_install + opt_uninstall + opt_help + opt_version;
		if (rv > 1) {
			fprintf(stderr, "thekraken: ERROR: choose either of '-i', '-u', '-h' or '-V'\n");
			return -1;
		}
		if (rv == 0) {
			opt_help = 1;
		}
		if (opt_install || opt_uninstall) {
			if (optind + 1 < ac) {
				fprintf(stderr, "thekraken: ERROR: parameter not recognized: %s\n", av[optind + 1]);
				return -1;
			}
		} else {
			if (optind < ac) {
				fprintf(stderr, "thekraken: ERROR: parameter not recognized: %s\n", av[optind]);
				return -1;
			}
		}
		if (opt_version == 1) {
			return 0;
		}
		if (opt_help == 1) {
			fprintf(stderr, "Usage:\n");
			fprintf(stderr, "\t%s [-v] [-y] [-n] [-c opt1=val1] [-c opt2=val2] [...] -i [path]\n", av[0]);
			fprintf(stderr, "\t%s [-v] [-y] [-n] -u [path]\n", av[0]);
			fprintf(stderr, "\t%s -h\n", av[0]);
			fprintf(stderr, "\t%s -V\n", av[0]);
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
			fprintf(stderr, "\t-n\t\tno modify mode\n");
			fprintf(stderr, "\t-c opt=val\tcreate configuration file with 'opt' variable\n");
			fprintf(stderr, "\t\t\tset to 'val'; can be specified multiple times\n");
			fprintf(stderr, "\t-V\t\tprint version information and exit\n");
			fprintf(stderr, "\t-h\t\tdisplay this help and exit\n");
			return 0;
		}
		if (optind < ac) {
			path = av[optind];
		}
		if (path == NULL) {
			path = ".";
		}
		if (opt_install) {
			fprintf(stderr, "thekraken: performing installation to %s\n", path);
			traverse(path, 0, (opt_yes ? OPT_YES : 0) | (opt_nomodify ? OPT_NOMODIFY : 0), &counter, &total);
			if (total == 0) {
				fprintf(stderr, "thekraken: finished installation, found no files to process\n");
			} else {
				fprintf(stderr, "thekraken: finished installation, %d out of %d files processed\n", counter, total);
			}
			return 0;
		}
		if (opt_uninstall) {
			fprintf(stderr, "thekraken: performing uninstallation from %s\n", path);
			traverse(path, 1, (opt_yes ? OPT_YES : 0) | (opt_nomodify ? OPT_NOMODIFY : 0), &counter, &total);
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
	logfp = fopen(LOGFN, "w");
	if (logfp) {
		setvbuf(logfp, NULL, _IONBF, 0);
		fprintf(stderr, WELCOME_LINES, get_build_info());
		fprintf(stderr, "thekraken: PID: %d\n", getpid());
		fprintf(stderr, "thekraken: Logging to " LOGFN "\n");
	} else {
		/* fail silently */
		logfp = stderr;
	}
	logfd = fileno(logfp);
	fprintf(logfp, WELCOME_LINES, get_build_info());
	fprintf(logfp, "thekraken: PID: %d\n", getpid());
	
	s = strrchr(av[0], '/');
	if (s) {
		len = s - av[0] + 1;
		memcpy(t, av[0], len);
		memcpy(u, av[0], len); /* dirname (for config file) */
		s++;
	} else {
		s = av[0];
	}
	t += len;
	nbin[sizeof(nbin) - 1] = '\0';
	snprintf(t, sizeof(nbin) - len - 1, INSTALL_FMT, s);
	fprintf(logfp, "thekraken: launch binary: %s\n", nbin);

	u += len;
	config[sizeof(config) - 1] = '\0';
	snprintf(u, sizeof(config) - len - 1, "%s", CONF_FN);
	fprintf(logfp, "thekraken: config file: %s\n", config);

	conf_file_parse(config);

	signal(SIGHUP, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGTSTP, sighandler);
	signal(SIGALRM, sigalrmhandler);

	/* set the last_used_cpu to the config setting (default: 0) */
	last_used_cpu = conf_startcpu;

	cpid = fork();
	if (cpid == -1) {
		fprintf(logfp, "thekraken: fork: %s\n", strerror(errno));
		return -1;
	}
	if (cpid == 0) {
		long prv;
		
		prv = ptrace(PTRACE_TRACEME, 0, 0, 0);
		if (prv == -1) {
			fprintf(logfp, "thekraken: child: ptrace(PTRACE_TRACEME) returns -1 (errno %d)\n", errno);
			return -1;
		}
		fprintf(logfp, "thekraken: child: ptrace(PTRACE_TRACEME) returns 0\n");
		fprintf(logfp, "thekraken: child: Executing...\n");
		execvp(nbin, av);
		fprintf(logfp, "thekraken: child: exec: %s\n", strerror(errno));
		return -1;
	}
		
	fprintf(logfp, "thekraken: Forked %d.\n", cpid);
	
	while (1) {
		int rv;

		rv = waitpid(-1, &status, __WALL);
		if (rv == -1) {
			fprintf(logfp, "thekraken: waitpid() returns -1 (errno %d)\n", errno);
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (rv != tpid && (rv != cpid || fahcore_logfd != -1)) /* ignore the talkative FahCore process or it will flood the log */
			fprintf(logfp, "thekraken: waitpid() returns %d with status 0x%08x\n", rv, status);

		if (WIFEXITED(status)) {
			fprintf(logfp, "thekraken: %d: exited with %d\n", rv, WEXITSTATUS(status));
			if (rv == mpid) {
				time_t runtime = time(NULL) - synthload_start_time;

				fprintf(logfp, "thekraken: %d: synthetic load manager exited (run time: %ld seconds)\n", rv, runtime);
				tpid = -1;
				continue;
			}
			if (rv != cpid) {
				fprintf(logfp, "thekraken: %d: ignoring clone exit\n", rv);
				continue;
			}
			return WEXITSTATUS(status);
		}
		if (WIFSIGNALED(status)) {
			/* fatal signal sent by user to/raised by underlying FahCore */
			fprintf(logfp, "thekraken: %d: terminated by signal %d\n", rv, WTERMSIG(status));

			if (rv == mpid) {
				time_t runtime = time(NULL) - synthload_start_time;
				
				fprintf(logfp, "thekraken: %d: synthetic load manager terminated (run time: %ld seconds)\n", rv, runtime);
				tpid = -1;
				continue;
			}
			if (rv != cpid) {
				fprintf(logfp, "thekraken: %d: ignoring clone termination\n", rv);
				continue;
			}
			signal(WTERMSIG(status), SIG_DFL);
			raise(WTERMSIG(status));
			return -1;
		}
		if (WIFSTOPPED(status)) {
			int e;
			long prv;
			int ptrace_request;

			if (rv != tpid && (rv != cpid || fahcore_logfd != -1)) /* ignore the talkative FahCore process or it will flood the log */
				fprintf(logfp, "thekraken: %d: stopped with signal 0x%08x\n", rv, WSTOPSIG(status));

			if (WSTOPSIG(status) == SIGTRAP) {
				long cloned = -1;
				e = status >> 16;

				if (nclones == -1 && e == 0) {
					/* initial attach */
					fprintf(logfp, "thekraken: %d: initial attach\n", rv);
					prv = ptrace(PTRACE_SETOPTIONS, rv, 0, PTRACE_O_TRACECLONE);
					fprintf(logfp, "thekraken: %d: Continuing.\n", rv);
					prv = ptrace(PTRACE_SYSCALL, rv, 0, 0);
					nclones++;
					continue;
				}

				if (e & PTRACE_EVENT_CLONE) {
					int c;

					prv = ptrace(PTRACE_GETEVENTMSG, rv, 0, &cloned);
					c = cloned;
					fprintf(logfp, "thekraken: %d: cloned %d\n", rv, c);
					nclones++;
					if (nclones != 2 && nclones != 3) {
						fprintf(logfp, "thekraken: %d: binding %d to cpu %d\n", rv, c, last_used_cpu);
						CPU_ZERO(&cpuset);
						CPU_SET(last_used_cpu, &cpuset);
						last_used_cpu++;
						sched_setaffinity(c, sizeof(cpuset), &cpuset);
					}
					if (nclones == 1) {
						if (conf_dlbload == 1) {
							fprintf(logfp, "thekraken: %d: talkative FahCore process identified (%d), listening to syscalls\n", rv, c);
							tpid = c;
						}
						if (conf_startup_deadline != 0) {
							fprintf(logfp, "thekraken: %d: startup deadline in %d seconds\n", rv, conf_startup_deadline);
							alarm(conf_startup_deadline);
							tpid = c;
						}
					}

					/*
					 * The following's quite dirty; we're relying on the fact that
					 * tpid clones add'l threads; if that wasn't the case, calling
					 * ptrace(PTRACE_SYSCALL, tpid, ...) would be challenging...
					 */
					if (rv == tpid || (rv == cpid && fahcore_logfd == -1)) {
						fprintf(logfp, "thekraken: %d: Continuing (SYSCALL).\n", rv);
						prv = ptrace(PTRACE_SYSCALL, rv, 0, 0);	
					} else {
						fprintf(logfp, "thekraken: %d: Continuing.\n", rv);
						prv = ptrace(PTRACE_CONT, rv, 0, 0);
					}
					continue;
				}

				if (rv == tpid) {
					/* this is the talkative fah process. Check for data written to stderr or the logfile (fd 5) */
					long call, fd, msgaddr, msglen;
					struct user_regs_struct regs;

					ptrace(PTRACE_GETREGS, rv, NULL, &regs);
					call = regs.orig_rax;
					fd = regs.rdi;
					msgaddr = regs.rsi;
					msglen = regs.rdx;

					if (call == SYS_write && fd == fahcore_logfd) {
						if (!tpid_insyscall) {
							tpid_insyscall = 1;
							getstr(rv, msgaddr, msglen, fahcore_logbuf, &fahcore_logbufpos, sizeof(fahcore_logbuf));
							if (strchr(fahcore_logbuf, '\n') != NULL) {
								if (strstr(fahcore_logbuf, "Completed ") != NULL && strstr(fahcore_logbuf, "out of") != NULL) {
									fprintf(logfp, "thekraken: %d: first step identified\n", rv);
									if (conf_dlbload) {
										int dlbload_workers = (nclones - 2) / 2;

										fprintf(logfp, "thekraken: %d: creating %d synthload workers: on %dms, off %dms, deadline %dms\n", rv, dlbload_workers, conf_dlbload_onperiod, conf_dlbload_offperiod, conf_dlbload_deadline);
										synthload_start_time = time(NULL);
										mpid = synthload_start(conf_dlbload_onperiod, conf_dlbload_offperiod, conf_dlbload_deadline, dlbload_workers, conf_startcpu);
										if (mpid < 0) {
											fprintf(logfp, "thekraken: %d: synthload_start failed: %s (rv: %d)\n", rv, strerror(errno), mpid);
											tpid = -1;
										}
										fprintf(logfp, "thekraken: %d: synthload manager created (%d)\n", rv, mpid);
									}
									if (conf_startup_deadline != 0) {
										fprintf(logfp, "thekraken: %d: startup complete\n", rv);
										alarm(0);
										if (!conf_dlbload) {
											tpid = -1;
										}
									}
								}
								fahcore_logbufpos = 0;
							}
							if (fahcore_logbufpos == sizeof(fahcore_logbuf) - 1) {
								fprintf(logfp, "thekraken: %d: log buffer overflow! Clearing the buffer.\n", rv);
								fahcore_logbufpos = 0;
							}
						} else {
							tpid_insyscall = 0;
						}
					} else if (call == SYS_write && fd == STDERR_FILENO) {
						if (!tpid_insyscall) {
							tpid_insyscall = 1;
							getstr(rv, msgaddr, msglen, fahcore_errbuf, &fahcore_errbufpos, sizeof(fahcore_errbuf));
							if (strchr(fahcore_errbuf, '\n') != NULL) {
								if (strstr(fahcore_errbuf, "Turning on dynamic load balancing") != NULL) {
									fprintf(logfp, "thekraken: %d: DLB has engaged; killing synthetic load manager\n", rv);
									kill(mpid, SIGTERM);
									tpid = -1; /* don't monitor the talkative thread anymore */
								}
								fahcore_errbufpos = 0;
							}
							if (fahcore_errbufpos == sizeof(fahcore_errbuf) - 1) {
								fprintf(logfp, "thekraken: %d: stderr buffer overflow! Clearing the buffer.\n", rv);
								fahcore_errbufpos = 0;
							}
						} else {
							tpid_insyscall = 0;
						}
					}

					/* talkative FahCore process; notify us of the next syscall entry/exit */
					prv = ptrace(PTRACE_SYSCALL, rv, 0, 0);	
					continue;
				}

				if (rv == cpid && fahcore_logfd == -1) {
					long call, fn, ret;
					struct user_regs_struct regs;

					ptrace(PTRACE_GETREGS, rv, NULL, &regs);
					call = regs.orig_rax;
					fn = regs.rdi;
					ret = regs.rax;

					if (call == SYS_open) {
						if (!cpid_insyscall) {
							cpid_insyscall = 1;
						} else {
							char buf[16];
							int bufpos = 0;

							cpid_insyscall = 0;

							getstr(rv, fn, -1, buf, &bufpos, sizeof(buf));
							if (!strncmp("work/logfile_", buf, 13)) {
								fprintf(logfp, "thekraken: %d: logfile fd: %ld\n", rv, ret);
								fahcore_logfd = ret;
							}
						}
					}

					prv = ptrace(PTRACE_SYSCALL, rv, 0, 0);	
					continue;
				}

				fprintf(logfp, "thekraken: %d: Continuing (unhandled trap).\n", rv);
				prv = ptrace(PTRACE_CONT, rv, 0, 0);
				continue;
			}

			if (rv == tpid || (rv == cpid && fahcore_logfd == -1)) {
				ptrace_request = PTRACE_SYSCALL;
			} else {
				ptrace_request = PTRACE_CONT;
			}

			if (rv == tpid) {
				tpid_insyscall = 0;
			} else if (rv == cpid && fahcore_logfd == -1) {
				cpid_insyscall = 0;
			}

			/*
			 * allow delivery of only one terminating signal
			 * to FahCore (per its sighandlers)
			 */
			if (WSTOPSIG(status) == SIGINT || WSTOPSIG(status) == SIGTERM) {
				if (shutdown) {
					fprintf(logfp, "thekraken: %d: Continuing%s.\n", rv, ptrace_request == PTRACE_SYSCALL ? " (SYSCALL)" : "");
					prv = ptrace(ptrace_request, rv, 0, 0);
					continue;
				}
				shutdown = 1;
				fprintf(logfp, "thekraken: %d: Continuing (forwarding signal %d)%s.\n", rv, WSTOPSIG(status), ptrace_request == PTRACE_SYSCALL ? " (SYSCALL)" : "");
				prv = ptrace(ptrace_request, rv, 0, WSTOPSIG(status));
				continue;
			}

			if (WSTOPSIG(status) == SIGSTOP) {
				fprintf(logfp, "thekraken: %d: Continuing%s.\n", rv, ptrace_request == PTRACE_SYSCALL ? " (SYSCALL)" : "");
				prv = ptrace(ptrace_request, rv, 0, 0);
				continue;
			}
			fprintf(logfp, "thekraken: %d: Continuing (forwarding signal %d)%s.\n", rv, WSTOPSIG(status), ptrace_request == PTRACE_SYSCALL ? " (SYSCALL)" : "");
			prv = ptrace(ptrace_request, rv, 0, WSTOPSIG(status));
			continue;
		}
		fprintf(logfp, "thekraken: %d: unknown waitpid status, halt!\n", rv);
	}
	return 0;
}
