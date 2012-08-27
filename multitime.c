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
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>



enum Format_Style {FORMAT_LIKE_TIME, FORMAT_NORMAL, FORMAT_MAXIMAL};

typedef struct {
    enum Format_Style format_style;
    unsigned int num_cmds;      // How many commands the user has specified.
    char ***cmds;               // The pre-processed execvp'able arguments for
                                // each command.
    struct timeval ***timevals; // The wall clock time for each command run.
    struct rusage ***rusages;   // The rusage each command run.
    unsigned int num_runs;      // How many times to run each command.
} Conf;

extern char* __progname;

void usage(int, char *);
void run_cmd(Conf *, unsigned int, unsigned int);
int cmp_timeval(const void *, const void *);
void format_like_time(Conf *);
void format_normal(Conf *);

#define TIMEVAL_TO_DOUBLE(t) \
  ((double) (t)->tv_sec + (double) (t)->tv_usec / 1000000)




void run_cmd(Conf *conf, unsigned int cmd_num, unsigned int cmd_i)
{
    int stdinp[2];
    if (pipe(stdinp) == -1)
        goto err;

    struct rusage *ru = conf->rusages[cmd_num][cmd_i] =
      malloc(sizeof(struct rusage));

    // Note: we want to do as little stuff in either parent or child between the
    // two gettimeofday calls, otherwise we might interfere with the timings.

    struct timeval startt;
    gettimeofday(&startt, NULL);
    pid_t pid = fork();
    if (pid == 0) {
        // Child
        if (dup2(stdinp[0], STDIN_FILENO) == -1)
            goto err;
        close(stdinp[1]);
        execvp(conf->cmds[cmd_num][0], conf->cmds[cmd_num]);
        goto err;
    }

    // Parent
    
    int status;
    wait4(pid, &status, 0, ru);
    struct timeval endt;
    gettimeofday(&endt, NULL);

    struct timeval *tv = conf->timevals[cmd_num][cmd_i] =
      malloc(sizeof(struct timeval));
    timersub(&endt, &startt, tv);

    return;

err:
    err(1, "Error when attempting to run %s.\n", conf->cmds[cmd_num][0]);
}



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



void format_normal(Conf *conf)
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

////////////////////////////////////////////////////////////////////////////////
// main
//

void usage(int rtn_code, char *msg)
{
    if (msg)
        fprintf(stderr, "%s\n", msg);
    fprintf(stderr, "Usage: %s [-f <liketime|maximal>] <num runs> <command> "
      "[<arg 1> ... <arg n>]\n", __progname);
    exit(rtn_code);
}


int main(int argc, char** argv)
{
    Conf *conf = malloc(sizeof(Conf));
    conf->format_style = FORMAT_NORMAL;

    int ch;
    while ((ch = getopt(argc, argv, "f:h")) != -1) {
        switch (ch) {
            case 'f':
                if (strcmp(optarg, "liketime") == 0)
                    conf->format_style = FORMAT_LIKE_TIME;
                else if (strcmp(optarg, "maximal") == 0)
                    conf->format_style = FORMAT_MAXIMAL;
                else
                    usage(1, "Unknown format style.");
                break;
            case 'h':
                usage(0, NULL);
            default:
                usage(1, NULL);
        }
    }
    argc -= optind;
    argv += optind;
    
    if (argc == 0) {
        // num_runs not specified.
        usage(1, NULL);
    }

    // Process num_runs.

    errno = 0;
    char *ep = argv[0] + strlen(argv[0]);
    long lval = strtoimax(argv[0], &ep, 10);
    if (argv[0][0] == '\0' || *ep != '\0')
        usage(1, "'num runs' not a valid number.");
    if ((errno == ERANGE && (lval == INTMAX_MIN || lval == INTMAX_MAX))
      || lval <= 0 || lval >= UINT_MAX)
        usage(1, "'num runs' out of range.");
    conf->num_runs = (unsigned int) lval;
    argc -= 1;
    argv += 1;

    // Process the command(s).

    if (argc == 0) {
        // Command not specified.
        usage(1, NULL);
    }
    conf->num_cmds = 1;
    if (conf->num_cmds > 1 && conf->format_style == FORMAT_LIKE_TIME)
        usage(1, "Can't run more than 1 command with -f liketime.");
    conf->cmds = malloc(sizeof(char **) * conf->num_cmds);
    conf->cmds[0] = argv;
    conf->rusages = malloc(sizeof(struct rusage **) * conf->num_cmds);
    conf->timevals = malloc(sizeof(struct timeval **) * conf->num_cmds);
    for (unsigned int i = 0; i < conf->num_runs; i += 1) {
        conf->rusages[i] = malloc(sizeof(struct rusage *) * conf->num_runs);
        memset(conf->rusages[i], 0, sizeof(struct rusage *) * conf->num_cmds);
        conf->timevals[i] = malloc(sizeof(struct timeval *) * conf->num_runs);
        memset(conf->timevals[i], 0, sizeof(struct timeval *) * conf->num_cmds);
    }
    
    for (unsigned int i = 0; i < conf->num_runs; i += 1) {
        run_cmd(conf, 0, i);
    }
    
    switch (conf->format_style) {
        case FORMAT_LIKE_TIME:
            format_like_time(conf);
            break;
        case FORMAT_NORMAL:
            format_normal(conf);
        case FORMAT_MAXIMAL:
            // XXX
            break;
    }
}
