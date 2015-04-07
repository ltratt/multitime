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

int cmp_timeval_as_double(const void *x, const void *y)
{
    double xx = *(double*)x, yy = *(double*)y;
    if (xx < yy) return -1;
    if (xx > yy) return  1;
    return 0;
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
// Statistical routines
//
double calculate_mean(double *values, int size)
{
    double mean = .0;
    for (int j = 0; j < size; j++) {
        mean += values[j];
    }
    mean /= (double)size;
    return mean;
}


double calculate_std_dev(double *values, int size)
{
    double mean = calculate_mean(values, size);
    double stddev = .0;
    for (int j = 0; j < size; j += 1) {
        stddev += pow(values[j] - mean, 2);
    }
    stddev = sqrt(stddev / size);
    return stddev;
}


double calculate_ci(double *values, int size, int confidence)
{
    double z_t = .0;
    if (size < 30) { // Use t-value.
        z_t = tvals[confidence - 1][size - 1];
    }
    else { // num_runs over 30, use Z value.
        z_t = zvals[confidence - 1];
    }
    double stddev = calculate_std_dev(values, size);
    return ((z_t * stddev) / sqrt(size));
}


double calculate_median(double *values, int size)
{
    double median;
    int mdl, mdr;
    if (size % 2 == 0) {
        mdl = size / 2 - 1; // Median left
        mdr = size / 2;     // Median right
    }
    else {
        mdl = size / 2;
        mdr = 0; // Unused
    }
    if (size % 2 == 0) {
        median = (values[mdl] + values[mdr]) / 2.0;
    }
    else
        median = values[mdl];
    return median;
}


////////////////////////////////////////////////////////////////////////////////
// Format routines
//

void format_like_time(Conf *conf)
{
    // Formatting like /usr/bin/time only makes sense if a single command is run.
    assert(conf->num_cmds == 1);

    struct timeval real, user, sys;
    timerclear(&real);
    timerclear(&user);
    timerclear(&sys);
    Cmd *cmd = conf->cmds[0];
    for (int i = 0; i < conf->num_runs; i += 1) {
        timeradd(&real, cmd->timevals[i],           &real);
        timeradd(&user, &cmd->rusages[0]->ru_utime, &user);
        timeradd(&sys,  &cmd->rusages[0]->ru_stime, &sys);
    }
	fprintf(stderr, "real %9lld.%02lld\n",
      (long long) (real.tv_sec / conf->num_runs), (long long) ((real.tv_usec / 10000) / conf->num_runs));
	fprintf(stderr, "user %9lld.%02lld\n",
      (long long) (user.tv_sec / conf->num_runs), (long long) ((user.tv_usec / 10000) / conf->num_runs));
	fprintf(stderr, "sys  %9lld.%02lld\n",
      (long long) (sys.tv_sec / conf->num_runs), (long long) ((sys.tv_usec / 10000) / conf->num_runs));
}



void format_other(Conf *conf)
{
    fprintf(stderr, "===> %s results\n", __progname);
    for (int i = 0; i < conf->num_cmds; i += 1) {
        Cmd *cmd = conf->cmds[i];

        if (i > 0)
            fprintf(stderr, "\n");
        fprintf(stderr, "%d: ", i + 1);
        pp_cmd(conf, cmd);
        fprintf(stderr, "\n");
        fprintf(stderr,
          "            Mean                Std.Dev.    Min         Median      Max\n");

        // Convert elapsed times for each run to a sorted array of doubles.
        double real_times[conf->num_runs];
        double user_times[conf->num_runs];
        double sys_times[conf->num_runs];
        for (int j = 0; j < conf->num_runs; j += 1) {
            real_times[j] = TIMEVAL_TO_DOUBLE(cmd->timevals[j]);
            user_times[j] = TIMEVAL_TO_DOUBLE(&cmd->rusages[j]->ru_utime);
            sys_times[j]  = TIMEVAL_TO_DOUBLE(&cmd->rusages[j]->ru_stime);
        }
        qsort(real_times, conf->num_runs, sizeof(double), cmp_timeval_as_double);
        qsort(user_times, conf->num_runs, sizeof(double), cmp_timeval_as_double);
        qsort(sys_times,  conf->num_runs, sizeof(double), cmp_timeval_as_double);

        // Means
        double mean_real = calculate_mean(real_times, conf->num_runs);
        double mean_user = calculate_mean(user_times, conf->num_runs);
        double mean_sys  = calculate_mean(sys_times, conf->num_runs);

        // Standard deviations
        double stddev_real = calculate_std_dev(real_times, conf->num_runs);
        double stddev_user = calculate_std_dev(user_times, conf->num_runs);
        double stddev_sys  = calculate_std_dev(sys_times, conf->num_runs);

        // Confidence intervals (without means)
        double ci_real = calculate_ci(real_times, conf->num_runs, conf->conf_level);
        double ci_user = calculate_ci(user_times, conf->num_runs, conf->conf_level);
        double ci_sys  = calculate_ci(sys_times, conf->num_runs, conf->conf_level);

        // Minimums, maximums and medians.
        double min_real = real_times[0];
        double max_real = real_times[conf->num_runs - 1];
        double md_real = calculate_median(real_times, conf->num_runs);

        double min_user = user_times[0];
        double max_user = user_times[conf->num_runs - 1];
        double md_user = calculate_median(user_times, conf->num_runs);;

        double min_sys = sys_times[0];
        double max_sys = sys_times[conf->num_runs - 1];
        double md_sys = calculate_median(sys_times, conf->num_runs);;

        // Print everything out
        fprintf(stderr, "real        %.3f+/-%-12.4f%-12.3f%-12.3f%-12.3f%-12.3f\n",
            mean_real,
            ci_real,
            stddev_real,
            min_real,
            md_real,
            max_real);
        fprintf(stderr, "user        %.3f+/-%-12.4f%-12.3f%-12.3f%-12.3f%-12.3f\n",
            mean_user,
            ci_user,
            stddev_user,
            min_user,
            md_user,
            max_user);
        fprintf(stderr, "sys         %.3f+/-%-12.4f%-12.3f%-12.3f%-12.3f%-12.3f\n",
            mean_sys,
            ci_sys,
            stddev_sys,
            min_sys,
            md_sys,
            max_sys);
        if (conf->format_style == FORMAT_NORMAL)
            continue;

        //
        // rusage output.
        //
        int mdl, mdr;
        if (conf->num_runs % 2 == 0) {
            mdl = conf->num_runs / 2 - 1; // Median left
            mdr = conf->num_runs / 2;     // Median right
        }
        else {
            mdl = conf->num_runs / 2;
            mdr = 0; // Unused
        }

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
