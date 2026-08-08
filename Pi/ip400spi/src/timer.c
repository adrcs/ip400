/*---------------------------------------------------------------------------
        Project:          Ip400Spi

        File Name:        timer.c

        Author:           root

        Creation Date:    Mar. 6, 2025

        Description:      Timer functions

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include "types.h"
#include "spidefs.h"
#include "logger.h"

#include "timer.h"

// local definitions
#define CLOCKID 	CLOCK_REALTIME
#define SIG_TIMER	SIGUSR1					// signal to be used for timer

// Timer function threads
struct timer_threads_t {
	BOOL			exit;
	void			(*exec_func)(void);			// function called by timer
	pthread_t		timer_fn;
	pthread_mutex_t	timer_mutex;				// timer mutex
	pthread_cond_t	timer_wait_cond;			// timer wait condition
	pthread_mutex_t	timer_exit_mutex;			// timer exit mutex
	pthread_cond_t	timer_exit_wait_cond;		// timbeforeer exit wait condition
} timer_threads;

// function called by timer
static void *timer_threads_fn(void *arg);

/*
 * handle the timer signal
 */
static void timer_signal(int sig, siginfo_t *si, void *uc)
{
    /* Not a timer signal? */
    if (!si || si->si_code != SI_TIMER)	{
        return;
    }

	pthread_mutex_lock(&timer_threads.timer_mutex);
	pthread_cond_signal(&timer_threads.timer_wait_cond);
	pthread_mutex_unlock(&timer_threads.timer_mutex);
}

/*
 * stop the timer
 */
void stopTimer(void)
{
	pthread_mutex_lock(&timer_threads.timer_exit_mutex);
	pthread_cond_signal(&timer_threads.timer_exit_wait_cond);
	pthread_mutex_unlock(&timer_threads.timer_exit_mutex);

	timer_threads.exit = TRUE;
}

/*
 * timer process: does not return until signaled to do soTIMER_THREADS;
 * call the function every interval in ms
 */
BOOL startTimer(int interval, void (*exec_function)(void))
{
    struct sigaction sa;
    struct sigevent sev;
    static sigset_t mask;
    long long freq_nanosecs;
    struct itimerspec its;
    timer_t timerid;

    // init background threads
    timer_threads.exit = FALSE;
    timer_threads.exec_func = exec_function;
    pthread_mutex_init(&timer_threads.timer_mutex, NULL);
    pthread_mutex_init(&timer_threads.timer_exit_mutex,  NULL);
    pthread_cond_init(&timer_threads.timer_wait_cond, NULL);
    pthread_cond_init(&timer_threads.timer_exit_wait_cond, NULL);

	// step 1: establish a handle for timer signal
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = timer_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIG_TIMER, &sa, NULL) == -1)    {
    	logger(LOG_DEBUG,"sigaction Error\n");
		return(FALSE);
	}

    // step 2: block it temporarily
    sigemptyset(&mask);
    sigaddset(&mask, SIG_TIMER);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) == -1)    {
    	logger(LOG_DEBUG,"sigprocmask Error\n");
		return(FALSE);
	}

    // step 3: create the timer
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIG_TIMER;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create((clockid_t)CLOCKID, &sev, &timerid) == -1)    {
        logger(LOG_DEBUG,"timer_create Error\n");
		return(FALSE);
	}

	// start the background thread
	pthread_create(&timer_threads.timer_fn, NULL, timer_threads_fn, (void *)&timer_threads);

    // now start the timer
    freq_nanosecs = MSTONS(interval);
    its.it_value.tv_sec = freq_nanosecs / 1000000000;
    its.it_value.tv_nsec = freq_nanosecs % 1000000000;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;
    if (timer_settime(timerid, 0, &its, NULL) == -1)    {
        logger(LOG_DEBUG,"timer_settime Error\n");
		return(FALSE);
    }

    // unblock the signal
    if (pthread_sigmask(SIG_UNBLOCK, &mask, NULL) == -1)    {
        logger(LOG_DEBUG,"sigprocmask Error\n");
		return(FALSE);
    }

    // wait for the signal to stop
	pthread_mutex_lock(&timer_threads.timer_exit_mutex);
    while(!timer_threads.exit)  {
        pthread_cond_wait(&timer_threads.timer_exit_wait_cond, &timer_threads.timer_exit_mutex);
    }
    pthread_mutex_unlock(&timer_threads.timer_exit_mutex);

    // stop the timer
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    timer_settime(timerid, 0, &its, NULL);

    // stop the background thread
    pthread_join(timer_threads.timer_fn, NULL);

    logger(LOG_DEBUG,"Timer process stopped\n");
	return TRUE;
}

// this is called at the timer interval...
static void *timer_threads_fn(void *arg)
{
	struct timer_threads_t *s = arg;

    // read the pipe every 20 ms
	while (!s->exit) {

		// wait for timer
		pthread_mutex_lock(&s->timer_mutex);
		pthread_cond_wait(&s->timer_wait_cond, &s->timer_mutex);
		pthread_mutex_unlock(&s->timer_mutex);

		// call function
		(s->exec_func)();
    }
	return NULL;
}
