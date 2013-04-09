/*
 * Copyright (C) 2012 by Stephen Gordon <firedfly@gmail.com>
 * Copyright (C) 2012 by Kris Rusocki <kszysiu@gmail.com>
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
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

static unsigned int _offperiod;
static timer_t load_timer, deadline_timer;

static void sigalrmhandler(int sig, siginfo_t *si, void *uc)
{
	if (si->si_value.sival_ptr == &deadline_timer) {
		_exit(0);
	}

	/* sleep for a bit.  the load timer will go off again every (onperiod + offperiod) */
	usleep(_offperiod * 1000);
}

static void sigchldhandler(int sig)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

static void load()
{
	while (1)
		sqrt(rand());
}

static void setup_alarms(unsigned int onperiod, unsigned int offperiod, unsigned int deadline)
{
	struct sigevent sigev_load, sigev_deadline;
	struct itimerspec its_load, its_deadline;

	/* create and start the load timer */
	sigev_load.sigev_notify = SIGEV_SIGNAL;
	sigev_load.sigev_signo = SIGALRM;
	sigev_load.sigev_value.sival_ptr = &load_timer;
	timer_create(CLOCK_REALTIME, &sigev_load, &load_timer);

	its_load.it_value.tv_sec = onperiod / 1000;
	its_load.it_value.tv_nsec = (onperiod % 1000) * 1000000;
	its_load.it_interval.tv_sec = onperiod / 1000 + offperiod / 1000;
	its_load.it_interval.tv_nsec = (onperiod % 1000 + offperiod % 1000) * 1000000;
	timer_settime(load_timer, 0, &its_load, NULL);

	/* create and start the deadline timer */
	sigev_deadline.sigev_notify = SIGEV_SIGNAL;
	sigev_deadline.sigev_signo = SIGALRM;
	sigev_deadline.sigev_value.sival_ptr = &deadline_timer;
	timer_create(CLOCK_REALTIME, &sigev_deadline, &deadline_timer);

	its_deadline.it_value.tv_sec = deadline / 1000;
	its_deadline.it_value.tv_nsec = (deadline % 1000) * 1000000;
	its_deadline.it_interval.tv_sec = 0;
	its_deadline.it_interval.tv_sec = 0;
	timer_settime(deadline_timer, 0, &its_deadline, NULL);
}

static void bindcpu(pid_t pid, int cpu)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	sched_setaffinity(pid, sizeof(cpuset), &cpuset);
}

static int create_workers(int workers, int startcpu, unsigned int onperiod, unsigned int offperiod, unsigned int deadline)
{
	int pid;

	for(; workers > 0; workers--) {
		pid = fork();
		if (pid == -1) {
			return -1;
		}
		if (pid == 0) {
			prctl(PR_SET_PDEATHSIG, SIGHUP);
			setup_alarms(onperiod, offperiod, deadline);
			load();
		}
		else {
			bindcpu(pid, startcpu);
			startcpu += 2;
		}
	}

	return 0;
}

/* 
 * Forks the kraken to create a manager for the synthload processes.  The manager 
 * then forks off workers to put a synthetic load on the system's CPUs.  The manager
 * and workers then go through a cycle of loading the CPU and sleeping.  This will
 * continue until either DLB is engaged (as detected by the kraken) or the deadline
 * is reached.
 *
 * onperiod  - number of ms to load the CPU during each load/sleep cycle
 * offperiod - number of ms to sleep before starting the next load/sleep cycle
 * deadline  - number of ms before the load/sleep cycle should stop
 * workers   - number of processes that should be loading CPUs
 * startcpu  - CPU number that the kraken started binding FahCore processes to
 */
pid_t synthload_start(unsigned int onperiod, unsigned int offperiod, unsigned int deadline, int workers, int startcpu)
{
	int mpid;
	struct sigaction sa;

	_offperiod = offperiod;

	/* fork the manager process */
	mpid = fork();
	if (mpid == -1) {
		return -1;
	}
	if (mpid == 0) {
		/* reset signals that defaulted from the main kraken process */
		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGHUP, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		
		signal(SIGCHLD, sigchldhandler);
		prctl(PR_SET_PDEATHSIG, SIGHUP);

		/* create the handler for SIGALRM so the handler will receive extra info */
		sa.sa_flags = SA_SIGINFO;
		sa.sa_sigaction = sigalrmhandler;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGALRM, &sa, NULL);

		if (create_workers(workers-1, startcpu+3, onperiod, offperiod, deadline) != 0)
			return -2;

		setup_alarms(onperiod, offperiod, deadline);
		load();
	} else {
		bindcpu(mpid, startcpu+1);
	}
	
	return mpid;
}
