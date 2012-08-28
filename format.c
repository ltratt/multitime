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
    for (int i = 0; i < conf->num_runs; i += 1) {
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



#define RUSAGE_CMP(n) int cmp_rusage_ ## n(const void *x, const void *y) \
    { \
        const long *l1 = &(*((const struct rusage **) x))->ru_ ## n; \
        const long *l2 = &(*((const struct rusage **) y))->ru_ ## n; \
        if (*l1 < *l2) \
            return -1; \
        else if (*l1 == *l2) \
            return 0; \
        return 1; \
    }

RUSAGE_CMP(maxrss)
RUSAGE_CMP(minflt)
RUSAGE_CMP(majflt)
RUSAGE_CMP(nswap)
RUSAGE_CMP(inblock)
RUSAGE_CMP(oublock)
RUSAGE_CMP(msgsnd)
RUSAGE_CMP(msgrcv)
RUSAGE_CMP(nsignals)
RUSAGE_CMP(nvcsw)
RUSAGE_CMP(nivcsw)



void format_other(Conf *conf)
{
    for (int i = 0; i < conf->num_cmds; i += 1) {
        fprintf(stderr,
          "            Min        Max        Mean       Std.Dev.   Median\n");

        // Means

        struct timeval mean_real_tv, mean_user_tv, mean_sys_tv;
        timerclear(&mean_real_tv);
        timerclear(&mean_user_tv);
        timerclear(&mean_sys_tv);
        for (int j = 0; j < conf->num_runs; j += 1) {
            timeradd(&mean_real_tv, conf->timevals[i][j],           &mean_real_tv);
            timeradd(&mean_user_tv, &conf->rusages[i][j]->ru_utime, &mean_user_tv);
            timeradd(&mean_sys_tv,  &conf->rusages[i][j]->ru_stime, &mean_sys_tv);
        }
        double mean_real = (double)
          TIMEVAL_TO_DOUBLE(&mean_real_tv) / conf->num_runs;
        double mean_user = (double)
          TIMEVAL_TO_DOUBLE(&mean_user_tv) / conf->num_runs;
        double mean_sys  = (double)
          TIMEVAL_TO_DOUBLE(&mean_sys_tv)  / conf->num_runs;

        // Standard deviations

        double real_stddev = 0, user_stddev = 0, sys_stddev = 0;
        for (int j = 0; j < conf->num_runs; j += 1) {
            real_stddev   +=
              pow(TIMEVAL_TO_DOUBLE(conf->timevals[i][j]) - mean_real, 2);
            user_stddev   +=
              pow(TIMEVAL_TO_DOUBLE(&conf->rusages[i][j]->ru_utime) - mean_user, 2);
            sys_stddev    +=
              pow(TIMEVAL_TO_DOUBLE(&conf->rusages[i][j]->ru_stime) - mean_sys,  2);
        }

        // Mins and maxes

        int mdl, mdr;
        if (conf->num_runs % 2 == 0) {
            mdl = conf->num_runs / 2 - 1; // Median left
            mdr = conf->num_runs / 2;     // Median right
        }
        else {
            mdl = conf->num_runs / 2;
            mdr = 0; // Unused
        }

        double min_real, max_real, md_real;
        qsort(conf->timevals[i], conf->num_runs,
          sizeof(struct timeval *), cmp_timeval);
        min_real = TIMEVAL_TO_DOUBLE(conf->timevals[i][0]);
        max_real = TIMEVAL_TO_DOUBLE(conf->timevals[i][conf->num_runs - 1]);
        if (conf->num_runs % 2 == 0) {
            struct timeval t;
            timeradd(conf->timevals[i][mdl], 
              conf->timevals[i][mdr], &t);
            md_real = TIMEVAL_TO_DOUBLE(&t) / 2;
        }
        else
            md_real = TIMEVAL_TO_DOUBLE(conf->timevals[i][mdl]);

        double min_user, max_user, md_user;
        qsort(conf->rusages[i], conf->num_runs,
          sizeof(struct rusage *), cmp_rusage_utime);
        min_user = TIMEVAL_TO_DOUBLE(&conf->rusages[i][0]->ru_utime);
        max_user = TIMEVAL_TO_DOUBLE(&conf->rusages[i][conf->num_runs - 1]->ru_utime);
        if (conf->num_runs % 2 == 0) {
            struct timeval t;
            timeradd(&conf->rusages[i][mdl]->ru_utime,
              &conf->rusages[i][mdr]->ru_utime, &t);
            md_user = TIMEVAL_TO_DOUBLE(&t) / 2;
        }
        else
            md_user = TIMEVAL_TO_DOUBLE(&conf->rusages[i][mdl]->ru_utime);

        double min_sys, max_sys, md_sys;
        qsort(conf->rusages[i],  conf->num_runs,
          sizeof(struct rusage *), cmp_rusage_stime);
        min_sys = TIMEVAL_TO_DOUBLE(&conf->rusages[i][0]->ru_stime);
        max_sys = TIMEVAL_TO_DOUBLE(&conf->rusages[i][conf->num_runs - 1]->ru_stime);
        if (conf->num_runs % 2 == 0) {
            struct timeval t;
            timeradd(&conf->rusages[i][mdl]->ru_stime,
              &conf->rusages[i][mdr]->ru_stime, &t);
            md_sys = TIMEVAL_TO_DOUBLE(&t) / 2;
        }
        else
            md_sys = TIMEVAL_TO_DOUBLE(&conf->rusages[i][mdl]->ru_stime);

        // Print everything out

	    fprintf(stderr, "real        %-11.3f%-11.3f%-11.3f%-11.3f%-11.3f\n",
          min_real, max_real, mean_real,
          sqrt(real_stddev / conf->num_runs), md_real);
	    fprintf(stderr, "user        %-11.3f%-11.3f%-11.3f%-11.3f%-11.3f\n",
          min_user, max_user, mean_user,
          sqrt(user_stddev / conf->num_runs), md_user);
	    fprintf(stderr, "sys         %-11.3f%-11.3f%-11.3f%-11.3f%-11.3f\n",
          min_sys, max_sys, mean_sys,
          sqrt(sys_stddev / conf->num_runs), md_sys);

        if (conf->format_style == FORMAT_NORMAL)
            return;

        //
        // rusage output.
        //

#       define RUSAGE_STAT(n) \
          long sum_##n = 0; \
          for (int j = 0; j < conf->num_runs; j += 1) \
              sum_##n += conf->rusages[i][j]->ru_##n; \
          long mean_##n = (double) sum_##n / conf->num_runs; \
          double stddev_##n = 0; \
          for (int j = 0; j < conf->num_runs; j += 1) \
              stddev_##n += pow(conf->rusages[i][j]->ru_##n - mean_##n, 2); \
          long min_##n, max_##n, md_##n; \
          qsort(conf->rusages[i], conf->num_runs, \
            sizeof(struct rusage *), cmp_rusage_##n); \
          min_##n = conf->rusages[i][0]->ru_##n; \
          max_##n = conf->rusages[i][conf->num_runs - 1]->ru_##n; \
          if (conf->num_runs % 2 == 0) \
              md_##n = (conf->rusages[i][mdl]->ru_##n + conf->rusages[i][mdr]->ru_##n) / 2; \
          else \
              md_##n = conf->rusages[i][mdl]->ru_##n; \
          fprintf(stderr, #n); \
          for (int j = 0; j < 12 - strlen(#n); j += 1) \
              fprintf(stderr, " "); \
          fprintf(stderr, "%-11ld%-11ld%-11ld%-11ld%-11ld\n", \
            min_##n, \
            max_##n, \
            mean_##n, \
            (long) sqrt(stddev_##n / conf->num_runs), \
            md_##n);

        RUSAGE_STAT(maxrss)
        RUSAGE_STAT(minflt)
        RUSAGE_STAT(majflt)
        RUSAGE_STAT(nswap)
        RUSAGE_STAT(inblock)
        RUSAGE_STAT(oublock)
        RUSAGE_STAT(msgsnd)
        RUSAGE_STAT(msgrcv)
        RUSAGE_STAT(nsignals)
        RUSAGE_STAT(nvcsw)
        RUSAGE_STAT(nivcsw)
    }
}
