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

#include <ctype.h>

#include <time.h>

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
#define INSTALL_FMT "thekraken-%s"

#define CONF_WARNING "#\n# WARNING: DO NOT MODIFY THIS FILE\n# Instead, uninstall The Kraken and re-install with desired configuration variables.\n#\n"
#define CONF_FN "thekraken.cfg"

static char *core_list[] = { CA3, CA3_SHORT, CA5, CA5_SHORT, NULL };

extern char **environ;
static int cpid;
static FILE *logfp;
static int logfd;

static int debug_level;
#define debug(_lev) if (debug_level >= _lev)

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

	snprintf(buf, sizeof(buf), "thekraken: got signal 0x%08x\n", n);
	write(logfd, buf, strlen(buf));
	kill(cpid, n);
}

#define FN_BUF_SIZE 24

static int conf_autorestart;
static time_t autorestart_start_time;
static time_t fs_time_offset;
static int autorestart_slot = -1;
#define AUTORESTART_POLLING_INTERVAL 10
static void sigalrmhandler(int n)
{
	char fn[FN_BUF_SIZE];
	char buf[STR_BUF_SIZE];
	struct stat st;
	time_t now;
	
	snprintf(fn, sizeof(fn), "work/wudata_%02u.ckp", autorestart_slot);
	now = time(NULL) + fs_time_offset;
	if (stat(fn, &st) == 0 && now - st.st_mtime >= 60 && st.st_mtime - autorestart_start_time > 60 * conf_autorestart) {
		snprintf(buf, sizeof(buf),
			"thekraken: autorestart: qualifying checkpoint identified "
			"(start: %ld, now: %ld, mtime: %ld, conf: %d), restarting.\n",
			autorestart_start_time,
			now,
			st.st_mtime,
			conf_autorestart);
		write(logfd, buf, strlen(buf));
		kill(cpid, SIGKILL);
		return;
	}
	alarm(AUTORESTART_POLLING_INTERVAL);
}

/*
 * Determine filesystem time offset (client residing on network file system)
 */
static int determine_fs_time_offset(void)
{
	int fd;
	struct stat st;

	fd = open(".thekraken-timeref", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd == -1) {
		return 1;
	}
	close(fd);
	if (stat(".thekraken-timeref", &st) != 0) {
		return 1;
	}
	fs_time_offset = st.st_mtime - time(NULL);

	return 0;
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

#define CONF_NP 0
#define CONF_AUTORESTART 1

static char **conf_line;
static int conf_index;
static int conf_total;
static int conf_step = 4;

static char *conf_key[] = { "np" , "autorestart", NULL };
static char *conf_val[sizeof(conf_key)/sizeof(char *)];

static int conf_np;

static void conf_line_add(char *s)
{
	if (conf_index == conf_total) {
		conf_total += conf_step;
		conf_step <<= 1;
		conf_line = realloc(conf_line, conf_total * sizeof(*conf_line));
	}
	conf_line[conf_index++] = s;
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
		return 1;
	}
	klen = e - s;
	if (klen >= sizeof(key)) {
		return 2;
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
			break;
		}
	}
	if (!conf_key[i]) {
		fprintf(logfp, "thekraken: WARNING: unknown configuration variable '%s'\n", key);
	}
	return 0;
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

	if (conf_val[CONF_NP]) {
		char *end;

		conf_np = strtol(conf_val[CONF_NP], &end, 10);
		if (*end != '\0' || conf_np < 2 || conf_np > 32) {
			fprintf(logfp, "thekraken: invalid value for 'np', ignored\n");
			conf_np = 0;
		} else {
			fprintf(logfp, "thekraken: config: np=%d\n", conf_np);
		}
	}
	
	if (conf_val[CONF_AUTORESTART]) {
		char *end;
		
		conf_autorestart = strtol(conf_val[CONF_AUTORESTART], &end, 10);
		if (*end != '\0' || conf_autorestart < 0) {
			fprintf(logfp, "thekraken: invalid value for 'autorestart', ignored\n");
			conf_autorestart = 0;
		} else {
			fprintf(logfp, "thekraken: config: autorestart=%d\n", conf_autorestart);
		}
	}
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

int main(int ac, char **av)
{
	char nbin[PATH_MAX];
	char *s, *t = nbin;
	int len = 0;
	char config[PATH_MAX];
	char *u = config;
	
	int status;

	int nclones = 0;
	int last_used_cpu = 0;
	cpu_set_t cpuset;
	
	int i;
	char **avclone;
	
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
					conf_line_parse(av[optind -1]);
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
	signal(SIGALRM, sigalrmhandler);

	/* prep avclone early, need it for autorestart feature */
	avclone = malloc((ac + 1) * sizeof(*avclone));
	for (i = 0; i < ac; i++) {
		avclone[i] = av[i];
		if (conf_np && !strcmp(av[i], "-np")) {
			avclone[i + 1] = conf_val[CONF_NP];
			i++;
		}
	}
	avclone[ac] = NULL;

	/* autorestart: check if restart is necessary */
	if (conf_autorestart) {
		fprintf(logfp, "thekraken: autorestart: examining work directory...\n");

		for (i = 0; i < 10; i++) {
			struct stat st;
			char fn[FN_BUF_SIZE];

			snprintf(fn, sizeof(fn), "work/wudata_%02u.dat", i);
			if (stat(fn, &st) == 0) {
				if (autorestart_slot != -1) {
					fprintf(logfp, "thekraken: autorestart: WARNING: multiple WUs in work directory (%u, %u, ...), bailing\n", autorestart_slot, i);
					conf_autorestart = 0;
					break;
				}
				autorestart_slot = i;
				snprintf(fn, sizeof(fn), "work/wudata_%02u.ckp", i);
				if (stat(fn, &st) == 0) {
					fprintf(logfp, "thekraken: autorestart: unit %u already carries a checkpoint, bailing\n", i);
					conf_autorestart = 0;
					break;
				}
			}
		}
		
		if (autorestart_slot == -1) {
			fprintf(logfp, "thekraken: autorestart: WARNING: no WUs found, bailing\n");
			conf_autorestart = 0;
		}
	}
	fprintf(logfp, "thekraken: autorestart is %s\n", conf_autorestart ? "on" : "off");
	if (conf_autorestart) {
		fprintf(logfp, "thekraken: autorestart slot: %u\n", autorestart_slot);
	}

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
		execvp(nbin, avclone);
		fprintf(logfp, "thekraken: child: exec: %s\n", strerror(errno));
		return -1;
	}
		
	fprintf(logfp, "thekraken: Forked %d.\n", cpid);
	
	if (conf_autorestart) {
		if (determine_fs_time_offset()) {
			fprintf(logfp, "thekraken: autorestart: WARNING: couldn't determine filesystem time offset, disabling autorestart\n");
			conf_autorestart = 0;
		} else {
			fprintf(logfp, "thekraken: autorestart: filesystem time offset: %ld seconds\n", fs_time_offset);
			autorestart_start_time = time(NULL) + fs_time_offset;
			alarm(AUTORESTART_POLLING_INTERVAL);
		}
	}

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
		fprintf(logfp, "thekraken: waitpid() returns %d with status 0x%08x\n", rv, status);
		if (WIFEXITED(status)) {
			fprintf(logfp, "thekraken: %d: exited with %d\n", rv, WEXITSTATUS(status));
			if (rv != cpid) {
				fprintf(logfp, "thekraken: %d: ignoring clone exit\n", rv);
				continue;
			}
			return WEXITSTATUS(status);
		}
		if (WIFSIGNALED(status)) {
			static int autorestart_done;

			/* signal sent by user to underlying FahCore */
			fprintf(logfp, "thekraken: %d: got signal %d\n", rv, WTERMSIG(status));
			
			/* autorestart */
			if (WTERMSIG(status) == SIGKILL && conf_autorestart && !autorestart_done) {
				nclones--;
				if (nclones >= 0)
					fprintf(logfp, "thekraken: autorestart enabled, %d thread(s) left\n", nclones);
				if (nclones != -1)
					continue;
				fprintf(logfp, "thekraken: main process exited, autorestarting...\n");

				/* restart state machine */
				nclones = 0;
				last_used_cpu = 0;

				cpid = fork();
				if (cpid == 0) {
					long prv;
					
					prv = ptrace(PTRACE_TRACEME, 0, 0, 0);
					if (prv == 0) {
						fprintf(logfp, "thekraken: child: ptrace(PTRACE_TRACEME) returns 0\n");
						fprintf(logfp, "thekraken: child: Executing...\n");
						execvp(nbin, avclone);
						fprintf(logfp, "thekraken: child: exec: %s\n", strerror(errno));
					} else {
						fprintf(logfp, "thekraken: child: ptrace(PTRACE_TRACEME) returns -1 (errno %d)\n", errno);
					}
				} else if (cpid != -1) {
					fprintf(logfp, "thekraken: Forked %d.\n", cpid);
					autorestart_done = 1;
					continue;
				}
			}
			raise(WTERMSIG(status));
			return -1;
		}
		if (WIFSTOPPED(status)) {
			int e;
			long prv;
			//struct user_regs_struct regs; // stack examination?

			fprintf(logfp, "thekraken: %d: stopped with signal 0x%08x\n", rv, WSTOPSIG(status));
			if (WSTOPSIG(status) == SIGTRAP) {
				long cloned = -1;
				e = status >> 16;
				if (e & PTRACE_EVENT_CLONE) {
					int c;

					prv = ptrace(PTRACE_GETEVENTMSG, rv, 0, &cloned);
					c = cloned;
					fprintf(logfp, "thekraken: %d: cloned %d\n", rv, c);
					nclones++;
					if (nclones != 2 && nclones != 3) {
						fprintf(logfp, "thekraken: %d: binding to cpu %d\n", c, last_used_cpu);
						CPU_ZERO(&cpuset);
						CPU_SET(last_used_cpu, &cpuset);
						last_used_cpu++;
						sched_setaffinity(c, sizeof(cpuset), &cpuset);
					}
				} else if (e == 0) {
					/* initial attach? */
					fprintf(logfp, "thekraken: %d: initial attach\n", rv);
					prv = ptrace(PTRACE_SETOPTIONS, rv, 0, PTRACE_O_TRACECLONE);
				}
				fprintf(logfp, "thekraken: %d: Continuing.\n", rv);
				prv = ptrace(PTRACE_CONT, rv, 0, 0);
				continue;
			}
			if (WSTOPSIG(status) == SIGSTOP) {
				fprintf(logfp, "thekraken: %d: Continuing.\n", rv);
				prv = ptrace(PTRACE_CONT, rv, 0, 0);
				continue;
			}
			fprintf(logfp, "thekraken: %d: Continuing (forwarding signal %d).\n", rv, WSTOPSIG(status));
			prv = ptrace(PTRACE_CONT, rv, 0, WSTOPSIG(status));
			continue;
		}
		fprintf(logfp, "thekraken: %d: unknown waitpid status, halt!\n", rv);
	}
	return 0;
}
