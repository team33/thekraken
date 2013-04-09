/*
 * Copyright (C) 2009,2010,2012 by Kris Rusocki <kszysiu@gmail.com>
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

#ifndef __LLOG_H
#define __LLOG_H

#include <sys/time.h>
#include <time.h>
#include <stdio.h>

extern FILE *logfp;
extern int debug_level;

#define llog(X, ...) do { \
				if (debug_level == 0 || logfp == stderr) { \
					if (logfp) { \
						fprintf(logfp, X, ##__VA_ARGS__); \
						fflush(logfp); \
					} \
				} else { \
					struct timeval __tv; \
					char *__s; \
					gettimeofday(&__tv, NULL); \
					__s = ctime(&__tv.tv_sec); \
					__s[strlen(__s) - 1] = '\0'; \
					if (logfp) { \
						if (debug_level == 1) \
							fprintf(logfp, "%s " X, __s, ##__VA_ARGS__); \
						else \
							fprintf(logfp, "[%lu.%06lu] " X, __tv.tv_sec, __tv.tv_usec, ##__VA_ARGS__); \
						fflush(logfp); \
					} \
				} \
			} while (0)

#define llogp(_logfd, _buf, _bufsize, X, ...) do { \
				if (debug_level == 0 || _logfd == 2) { \
					snprintf(_buf, _bufsize, X, ##__VA_ARGS__); \
				} else { \
					struct timeval __tv; \
					char *__s; \
					int __len = 0; \
					gettimeofday(&__tv, NULL); \
					if (debug_level == 1) { \
						ctime_r(&__tv.tv_sec, _buf); \
						__len = strlen(_buf) - 1; \
						__s = _buf + __len; \
						snprintf(__s, _bufsize - __len, " " X, ##__VA_ARGS__); \
					} else { \
						snprintf(_buf, _bufsize, "[%lu.%06lu] " X, __tv.tv_sec, __tv.tv_usec, ##__VA_ARGS__); \
					} \
				} \
			} while (0)

#define debug(_lev) if (debug_level >= _lev)

#endif
