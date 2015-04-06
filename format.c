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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>

#include "multitime.h"
#include "tvals.h"
#include "zvals.h"

extern char* __progname;

#define TIMEVAL_TO_DOUBLE(t) \
  ((double) (t)->tv_sec + (double) (t)->tv_usec / 1000000)

void pp_cmd(Conf *, Cmd *);
void pp_arg(const char *);



void pp_cmd(Conf *conf, Cmd *cmd)
{
    // Pretty-print the commands argv: we try to be semi-sensible about
    // escaping strings, but it's never going to be perfect, as the rules
    // are somewhat shell dependent.

    if (cmd->replace_str) {
        fprintf(stderr, "-I ");
        pp_arg(cmd->replace_str);
        fprintf(stderr, " ");
    }

    if (cmd->input_cmd) {
        fprintf(stderr, "-i ");
        pp_arg(cmd->input_cmd);
        fprintf(stderr, " ");
    }

    if (cmd->pre_cmd) {
        fprintf(stderr, "-r ");
        pp_arg(cmd->pre_cmd);
        fprintf(stderr, " ");
    }

    if (cmd->output_cmd) {
        fprintf(stderr, "-o ");
        pp_arg(cmd->output_cmd);
        fprintf(stderr, " ");
    }

    if (cmd->quiet_stderr)
        fprintf(stderr, "-qq ");
    else if (cmd->quiet_stdout)
        fprintf(stderr, "-q ");

    for (int i = 0; cmd->argv[i] != NULL; i += 1) {
        if (i > 0)
            fprintf(stderr, " ");
        pp_arg(cmd->argv[i]);
    }
}



void pp_arg(const char *s)
{
    if (strchr(s, ' ') == NULL)
        fprintf(stderr, "%s", s);
    else {
        fprintf(stderr, "\"");
        for (int k = 0; k < strlen(s); k += 1) {
            if (s[k] == '\"')
                fprintf(stderr, "\\\"");
            else
                fprintf(stderr, "%c", s[k]);
        }
        fprintf(stderr, "\"");
    }
}



////////////////////////////////////////////////////////////////////////////////
// Comparison commands
//
// These are needed for the various calls to quicksort in format_other
//

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



int cmp_cputime(const void *x, const void *y)
{
    return (*(double*)x - *(double*)y);
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


////////////////////////////////////////////////////////////////////////////////
// Format routines
//

void format_like_time(Conf *conf)
{
    // Formatting like /usr/bin/time only makes sense if a single command is run.
    assert(conf->num_cmds == 1);

    struct timeval real, user, sys, cpu;
    timerclear(&real);
    timerclear(&user);
    timerclear(&sys);
    timerclear(&cpu);
    Cmd *cmd = conf->cmds[0];
    for (int i = 0; i < conf->num_runs; i += 1) {
        timeradd(&real, cmd->timevals[i],           &real);
        timeradd(&user, &cmd->rusages[0]->ru_utime, &user);
        timeradd(&sys,  &cmd->rusages[0]->ru_stime, &sys);
        timeradd(&cpu,  &cmd->rusages[0]->ru_utime, &cpu);
        timeradd(&cpu,  &cmd->rusages[0]->ru_stime, &cpu);
    }
    fprintf(stderr, "real %9lld.%02lld\n",
        (long long) (real.tv_sec / conf->num_runs), (long long) ((real.tv_usec / 10000) / conf->num_runs));
    fprintf(stderr, "user %9lld.%02lld\n",
        (long long) (user.tv_sec / conf->num_runs), (long long) ((user.tv_usec / 10000) / conf->num_runs));
    fprintf(stderr, "sys  %9lld.%02lld\n",
        (long long) (sys.tv_sec / conf->num_runs), (long long) ((sys.tv_usec / 10000) / conf->num_runs));
    fprintf(stderr, "cpu  %9lld.%02lld\n",
        (long long) (cpu.tv_sec / conf->num_runs), (long long) ((cpu.tv_usec / 10000) / conf->num_runs));
}



void format_other(Conf *conf)
{
    double z_t = .0; // Z or t-value used to calculate confidence interval.
    fprintf(stderr, "===> %s results\n", __progname);
    for (int i = 0; i < conf->num_cmds; i += 1) {
        Cmd *cmd = conf->cmds[i];

        if (conf->num_runs < 30) { // Use t-value.
            z_t = tvals[conf->conf_level - 1][conf->num_runs - 1];
        }
        else { // num_runs over 30, use Z value.
            z_t = zvals[conf->conf_level - 1];
        }

        if (i > 0)
            fprintf(stderr, "\n");
        fprintf(stderr, "%d: ", i + 1);
        pp_cmd(conf, cmd);
        fprintf(stderr, "\n");
        fprintf(stderr,
          "            Mean                Std.Dev.    Min         Median      Max\n");

        // Means

        struct timeval mean_wall_tv, mean_user_tv, mean_sys_tv, mean_cpu_tv;
        timerclear(&mean_wall_tv);
        timerclear(&mean_user_tv);
        timerclear(&mean_sys_tv);
        timerclear(&mean_cpu_tv);
        for (int j = 0; j < conf->num_runs; j += 1) {
            timeradd(&mean_wall_tv, cmd->timevals[j],           &mean_wall_tv);
            timeradd(&mean_user_tv, &cmd->rusages[j]->ru_utime, &mean_user_tv);
            timeradd(&mean_sys_tv,  &cmd->rusages[j]->ru_stime, &mean_sys_tv);
            timeradd(&mean_cpu_tv,  &cmd->rusages[j]->ru_utime, &mean_cpu_tv);
            timeradd(&mean_cpu_tv,  &cmd->rusages[j]->ru_stime, &mean_cpu_tv);
        }
        double mean_wall = (double)
          TIMEVAL_TO_DOUBLE(&mean_wall_tv) / conf->num_runs;
        double mean_user = (double)
          TIMEVAL_TO_DOUBLE(&mean_user_tv) / conf->num_runs;
        double mean_sys  = (double)
          TIMEVAL_TO_DOUBLE(&mean_sys_tv)  / conf->num_runs;
        double mean_cpu  = (double)
          TIMEVAL_TO_DOUBLE(&mean_cpu_tv)  / conf->num_runs;

        // Standard deviations

        double wall_stddev = 0, user_stddev = 0, sys_stddev = 0, cpu_stddev = 0;
        for (int j = 0; j < conf->num_runs; j += 1) {
            wall_stddev   +=
              pow(TIMEVAL_TO_DOUBLE(cmd->timevals[j]) - mean_wall, 2);
            user_stddev   +=
              pow(TIMEVAL_TO_DOUBLE(&cmd->rusages[j]->ru_utime) - mean_user, 2);
            sys_stddev    +=
              pow(TIMEVAL_TO_DOUBLE(&cmd->rusages[j]->ru_stime) - mean_sys,  2);
            cpu_stddev   +=
              pow(TIMEVAL_TO_DOUBLE(&cmd->rusages[j]->ru_utime) +
                  TIMEVAL_TO_DOUBLE(&cmd->rusages[j]->ru_stime) - mean_cpu, 2);
        }

        // Confidence intervals (without means)

        double wall_ci = .0, user_ci = .0, sys_ci = .0, cpu_ci = .0;
        wall_ci = ((z_t * wall_stddev) / sqrt(conf->num_runs));
        user_ci = ((z_t * user_stddev) / sqrt(conf->num_runs));
        sys_ci = ((z_t * sys_stddev) / sqrt(conf->num_runs));
        cpu_ci = ((z_t * cpu_stddev) / sqrt(conf->num_runs));

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

        double min_wall, max_wall, md_wall;
        qsort(cmd->timevals, conf->num_runs, sizeof(struct timeval *),
          cmp_timeval);
        min_wall = TIMEVAL_TO_DOUBLE(cmd->timevals[0]);
        max_wall = TIMEVAL_TO_DOUBLE(cmd->timevals[conf->num_runs - 1]);
        if (conf->num_runs % 2 == 0) {
            struct timeval t;
            timeradd(cmd->timevals[mdl], cmd->timevals[mdr], &t);
            md_wall = TIMEVAL_TO_DOUBLE(&t) / 2;
        }
        else
            md_wall = TIMEVAL_TO_DOUBLE(cmd->timevals[mdl]);

        double min_user, max_user, md_user;
        qsort(cmd->rusages, conf->num_runs,
          sizeof(struct rusage *), cmp_rusage_utime);
        min_user = TIMEVAL_TO_DOUBLE(&cmd->rusages[0]->ru_utime);
        max_user = TIMEVAL_TO_DOUBLE(
          &cmd->rusages[conf->num_runs - 1]->ru_utime);
        if (conf->num_runs % 2 == 0) {
            struct timeval t;
            timeradd(&cmd->rusages[mdl]->ru_utime,
              &cmd->rusages[mdr]->ru_utime, &t);
            md_user = TIMEVAL_TO_DOUBLE(&t) / 2;
        }
        else
            md_user = TIMEVAL_TO_DOUBLE(&cmd->rusages[mdl]->ru_utime);

        double min_sys, max_sys, md_sys;
        qsort(cmd->rusages,  conf->num_runs,
          sizeof(struct rusage *), cmp_rusage_stime);
        min_sys = TIMEVAL_TO_DOUBLE(&cmd->rusages[0]->ru_stime);
        max_sys = TIMEVAL_TO_DOUBLE(&cmd->rusages[conf->num_runs - 1]->ru_stime);
        if (conf->num_runs % 2 == 0) {
            struct timeval t;
            timeradd(&cmd->rusages[mdl]->ru_stime,
              &cmd->rusages[mdr]->ru_stime, &t);
            md_sys = TIMEVAL_TO_DOUBLE(&t) / 2;
        }
        else
            md_sys = TIMEVAL_TO_DOUBLE(&cmd->rusages[mdl]->ru_stime);

        double min_cpu = .0, max_cpu = .0, md_cpu = .0;
        double cpu_times[conf->num_runs];
        for (int j = 0; j < conf->num_runs; j += 1) {
            cpu_times[j] = (TIMEVAL_TO_DOUBLE(&cmd->rusages[j]->ru_utime) +
                            TIMEVAL_TO_DOUBLE(&cmd->rusages[j]->ru_stime));
        }
        qsort(cpu_times, conf->num_runs, sizeof(double), cmp_cputime);
        min_cpu = cpu_times[0];
        max_cpu = cpu_times[conf->num_runs - 1];
        if (conf->num_runs % 2 == 0) {
            md_cpu = (cpu_times[mdl] + cpu_times[mdr]) / 2.0;
        }
        else
            md_cpu = cpu_times[mdl];

        // Print everything out

      fprintf(stderr, "wall        %.3f+/-%-12.4f%-12.3f%-12.3f%-12.3f%-12.3f\n",
          mean_wall,
          wall_ci,
          sqrt(wall_stddev / conf->num_runs),
          min_wall,
          md_wall,
          max_wall);
      fprintf(stderr, "cpu         %.3f+/-%-12.4f%-12.3f%-12.3f%-12.3f%-12.3f\n",
          mean_cpu,
          cpu_ci,
          sqrt(cpu_stddev / conf->num_runs),
          min_cpu,
          md_cpu,
          max_cpu);
      fprintf(stderr, "user        %.3f+/-%-12.4f%-12.3f%-12.3f%-12.3f%-12.3f\n",
          mean_user,
          user_ci,
          sqrt(user_stddev / conf->num_runs),
          min_user,
          md_user,
          max_user);
      fprintf(stderr, "sys         %.3f+/-%-12.4f%-12.3f%-12.3f%-12.3f%-12.3f\n",
          mean_sys,
          sys_ci,
          sqrt(sys_stddev / conf->num_runs),
          min_sys,
          md_sys,
          max_sys);
        if (conf->format_style == FORMAT_NORMAL)
            continue;

        //
        // rusage output.
        //

#       define RUSAGE_STAT(n) \
          long sum_##n = 0; \
          for (int j = 0; j < conf->num_runs; j += 1) \
              sum_##n += cmd->rusages[j]->ru_##n; \
          long mean_##n = (double) sum_##n / conf->num_runs; \
          double stddev_##n = 0; \
          for (int j = 0; j < conf->num_runs; j += 1) \
              stddev_##n += pow(cmd->rusages[j]->ru_##n - mean_##n, 2); \
          long min_##n, max_##n, md_##n; \
          qsort(cmd->rusages, conf->num_runs, \
            sizeof(struct rusage *), cmp_rusage_##n); \
          min_##n = cmd->rusages[0]->ru_##n; \
          max_##n = cmd->rusages[conf->num_runs - 1]->ru_##n; \
          if (conf->num_runs % 2 == 0) \
              md_##n = (cmd->rusages[mdl]->ru_##n + \
                cmd->rusages[mdr]->ru_##n) / 2; \
          else \
              md_##n = cmd->rusages[mdl]->ru_##n; \
          fprintf(stderr, #n); \
          for (int j = 0; j < 12 - strlen(#n); j += 1) \
              fprintf(stderr, " "); \
          fprintf(stderr, "%-12ld%-12ld%-12ld%-12ld%-12ld\n", \
            mean_##n, \
            (long) sqrt(stddev_##n / conf->num_runs), \
            min_##n, \
            md_##n, \
            max_##n);

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
