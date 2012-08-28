// Copyright (C)2008-2012 Laurence Tratt http://tratt.net/laurie/
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.


#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>

#include "multitime.h"


#define TIMEVAL_TO_DOUBLE(t) \
  ((double) (t)->tv_sec + (double) (t)->tv_usec / 1000000)


void format_like_time(Conf *conf)
{
    // Formatting like /usr/bin/time only makes sense if a single command is run.
    assert(conf->num_cmds == 1);

    struct timeval real, user, sys;
    timerclear(&real);
    timerclear(&user);
    timerclear(&sys);
    for (unsigned int i = 0; i < conf->num_runs; i += 1) {
        timeradd(&real, conf->timevals[0][i],           &real);
        timeradd(&user, &conf->rusages[0][i]->ru_utime, &user);
        timeradd(&sys,  &conf->rusages[0][i]->ru_stime, &sys);
    }
	fprintf(stderr, "real %9ld.%02ld\n",
      real.tv_sec / conf->num_runs, (real.tv_usec / 10000) / conf->num_runs);
	fprintf(stderr, "user %9ld.%02ld\n",
      user.tv_sec / conf->num_runs, (user.tv_usec / 10000) / conf->num_runs);
	fprintf(stderr, "sys  %9ld.%02ld\n",
      sys.tv_sec / conf->num_runs,  (sys.tv_usec / 10000) / conf->num_runs);
}



int cmp_timeval(const void *x, const void *y)
{
    const struct timeval *t1 = *((const struct timeval **) x);
    const struct timeval *t2 = *((const struct timeval **) y);

    if (t1->tv_sec < t2->tv_sec
      || (t1->tv_sec == t2->tv_sec && t1->tv_usec < t2->tv_usec))
        return -1;
    else if (t1->tv_sec == t2->tv_sec && t1->tv_usec == t2->tv_usec)
        return 0;
    else
        return 1;
}



int cmp_rusage_utime(const void *x, const void *y)
{
    const struct timeval *t1 = &(*((const struct rusage **) x))->ru_utime;
    const struct timeval *t2 = &(*((const struct rusage **) y))->ru_utime;

    return cmp_timeval(&t1, &t2);
}



int cmp_rusage_stime(const void *x, const void *y)
{
    const struct timeval *t1 = &(*((const struct rusage **) x))->ru_stime;
    const struct timeval *t2 = &(*((const struct rusage **) y))->ru_stime;

    return cmp_timeval(&t1, &t2);
}



void format_other(Conf *conf)
{
    fprintf(stderr,
      "          Min        Max        Mean       Std.Dev.   Median\n");

    struct timeval real_mean_tv, user_mean_tv, sys_mean_tv;
    timerclear(&real_mean_tv);
    timerclear(&user_mean_tv);
    timerclear(&sys_mean_tv);
    for (unsigned int i = 0; i < conf->num_runs; i += 1) {
        timeradd(&real_mean_tv, conf->timevals[0][i],           &real_mean_tv);
        timeradd(&user_mean_tv, &conf->rusages[0][i]->ru_utime, &user_mean_tv);
        timeradd(&sys_mean_tv,  &conf->rusages[0][i]->ru_stime, &sys_mean_tv);
    }
    double real_mean =
      TIMEVAL_TO_DOUBLE(&real_mean_tv) / (double) conf->num_runs;
    double user_mean =
      TIMEVAL_TO_DOUBLE(&user_mean_tv) / (double) conf->num_runs;
    double sys_mean  =
      TIMEVAL_TO_DOUBLE(&sys_mean_tv)  / (double) conf->num_runs;

    double real_stddev = 0, user_stddev = 0, sys_stddev = 0;
    for (unsigned int i = 0; i < conf->num_runs; i += 1) {
        real_stddev +=
          pow(TIMEVAL_TO_DOUBLE(conf->timevals[0][i]) - real_mean, 2);
        user_stddev +=
          pow(TIMEVAL_TO_DOUBLE(&conf->rusages[0][i]->ru_utime) - user_mean, 2);
        sys_stddev  +=
          pow(TIMEVAL_TO_DOUBLE(&conf->rusages[0][i]->ru_stime) - sys_mean,  2);
    }

    qsort(conf->timevals[0], conf->num_runs,
      sizeof(struct timeval *), cmp_timeval);
    qsort(conf->rusages[0],  conf->num_runs,
      sizeof(struct rusage *), cmp_rusage_utime);
    qsort(conf->rusages[0],  conf->num_runs,
      sizeof(struct rusage *), cmp_rusage_stime);
    
    double md_real, md_user, md_sys;
    if (conf->num_runs % 2 == 0) {
        int mdl = conf->num_runs / 2 - 1; // Median left
        int mdr = conf->num_runs / 2;     // Median right
        struct timeval t;
        timeradd(conf->timevals[0][mdl], 
          conf->timevals[0][mdr], &t);
        md_real = TIMEVAL_TO_DOUBLE(&t) / 2;
        timeradd(&conf->rusages[0][mdl]->ru_utime,
          &conf->rusages[0][mdr]->ru_utime, &t);
        md_user = TIMEVAL_TO_DOUBLE(&t) / 2;
        timeradd(&conf->rusages[0][mdl]->ru_stime,
          &conf->rusages[0][mdr]->ru_stime, &t);
        md_sys = TIMEVAL_TO_DOUBLE(&t) / 2;
    }
    else {
        int md = conf->num_runs / 2;
        md_real = TIMEVAL_TO_DOUBLE(conf->timevals[0][md]);
        md_user = TIMEVAL_TO_DOUBLE(&conf->rusages[0][md]->ru_utime);
        md_sys  = TIMEVAL_TO_DOUBLE(&conf->rusages[0][md]->ru_stime);
    }

	fprintf(stderr, "real      %-11.3f%-11.3f%-11.3f%-11.3f%-11.3f\n",
      TIMEVAL_TO_DOUBLE(conf->timevals[0][0]),
      TIMEVAL_TO_DOUBLE(conf->timevals[0][conf->num_runs - 1]),
      real_mean,
      sqrt(real_stddev / conf->num_runs),
      md_real);
	fprintf(stderr, "user      %-11.3f%-11.3f%-11.3f%-11.3f%-11.3f\n",
      TIMEVAL_TO_DOUBLE(&conf->rusages[0][0]->ru_utime),
      TIMEVAL_TO_DOUBLE(&conf->rusages[0][conf->num_runs - 1]->ru_utime),
      user_mean,
      sqrt(user_stddev / conf->num_runs),
      md_user);
	fprintf(stderr, "sys       %-11.3f%-11.3f%-11.3f%-11.3f%-11.3f\n",
      TIMEVAL_TO_DOUBLE(&conf->rusages[0][0]->ru_stime),
      TIMEVAL_TO_DOUBLE(&conf->rusages[0][conf->num_runs - 1]->ru_stime),
      sys_mean,
      sqrt(sys_stddev / conf->num_runs),
      md_sys);
}
