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


#include "Config.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "multitime.h"
#include "format.h"



#define BUFFER_SIZE (64 * 1024)


extern char* __progname;

void usage(int, char *);
void run_cmd(Conf *, int, int);
FILE *read_input(Conf *, int);
bool fcopy(FILE *, FILE *);
char *replace(Conf *, const char *, int);



////////////////////////////////////////////////////////////////////////////////
// Running commands
//

#include <fcntl.h>

void run_cmd(Conf *conf, int cmd_num, int cmd_i)
{
    FILE *tmpf = NULL;
    if (conf->input_cmd)
        tmpf = read_input(conf, cmd_i);

    FILE *outtmpf = NULL;
    char *output_cmd = replace(conf, conf->output_cmd, cmd_i);
    if (output_cmd) {
        outtmpf = tmpfile();
        if (!outtmpf)
            goto cmd_err;
    }

    struct rusage *ru = conf->rusages[cmd_num][cmd_i] =
      malloc(sizeof(struct rusage));

    // Note: we want to do as little stuff in either parent or child between the
    // two gettimeofday calls, otherwise we might interfere with the timings.

    struct timeval startt;
    gettimeofday(&startt, NULL);
    pid_t pid = fork();
    if (pid == 0) {
        // Child
        if (tmpf && dup2(fileno(tmpf), STDIN_FILENO) == -1)
            goto cmd_err;

        if (conf->quiet && freopen("/dev/null", "w", stdout) == NULL)
            goto cmd_err;
        else if (output_cmd && dup2(fileno(outtmpf), STDOUT_FILENO) == -1)
            goto cmd_err;
        execvp(conf->cmds[cmd_num][0], conf->cmds[cmd_num]);
        goto cmd_err;
    }

    // Parent
    
    int status;
    wait4(pid, &status, 0, ru);
    struct timeval endt;
    gettimeofday(&endt, NULL);

    if (conf->input_cmd)
        fclose(tmpf);

    struct timeval *tv = conf->timevals[cmd_num][cmd_i] =
      malloc(sizeof(struct timeval));
    timersub(&endt, &startt, tv);

    // If an output command is specified, pipe the temporary output to it, and
    // check its return code.

    if (output_cmd) {
        fflush(outtmpf);
        fseek(outtmpf, 0, SEEK_SET);
        FILE *cmdf = popen(output_cmd, "w");
        if (cmdf == NULL || !fcopy(outtmpf, cmdf))
            goto output_cmd_err;
        int cmdr = pclose(cmdf);
        if (cmdr != 0)
            errx(1, "Exiting because '%s' failed.", output_cmd);
        fclose(outtmpf);
        free(output_cmd);
    }

    return;

cmd_err:
    err(1, "Error when attempting to run %s", conf->cmds[cmd_num][0]);

output_cmd_err:
    err(1, "Error when attempting to run %s", output_cmd);
}



//
// Read in the input from conf->input_cmd for cmd_i and return an open file set
// to read from the beginning which contains its output.
//

FILE *read_input(Conf *conf, int cmd_i)
{
    assert(conf->input_cmd);

    char *input_cmd = replace(conf, conf->input_cmd, cmd_i);
    FILE *cmdf = popen(input_cmd, "r");
    FILE *tmpf = tmpfile();
    if (!cmdf || !tmpf)
        goto cmd_err;
    
    fcopy(cmdf, tmpf);
    pclose(cmdf);
    free(input_cmd);
    fseek(tmpf, 0, SEEK_SET);
    
    return tmpf;

cmd_err:
    err(1, "Error when attempting to run %s.\n", conf->input_cmd);
}



//
// Copy all data from rf to wf. Returns true if successful, false if not.
//

bool fcopy(FILE *rf, FILE *wf)
{
    char *buf = malloc(BUFFER_SIZE);
    while (1) {
        size_t r = fread(buf, 1, BUFFER_SIZE, rf);
        if (r < BUFFER_SIZE && ferror(rf)) {
            free(buf);
            return false;
        }
        size_t w = fwrite(buf, 1, r, wf);
        if (w < r && ferror(wf)) {
            free(buf);
            return false;
        }
        if (feof(rf))
            break;
    }
    free(buf);

    return true;
}



//
// Take in string 's' and replace all instances of conf->replace_str with
// str(cmd_i). Always returns a malloc'd string (even if conf->replace_str is not
// in s) which must be manually freed *except* if s is NULL, whereupon NULL is
// returned.
//

char *replace(Conf *conf, const char *s, int cmd_i)
{
    if (s == NULL)
        return NULL;

    char *rtn;
    if (!conf->replace_str) {
        rtn = malloc(strlen(s) + 1);
        memmove(rtn, s, strlen(s));
        rtn[strlen(s)] = 0;
    }
    else {
        int replacen = 0;
        const char *f = s;
        while (true) {
            f = strstr(f, conf->replace_str);
            if (f == NULL)
                break;
            replacen++;
            f += strlen(conf->replace_str);
        }
        int nch = snprintf(NULL, 0, "%d", cmd_i);
        char buf1[nch + 1];
        snprintf(buf1, nch + 1, "%d", cmd_i);
        rtn = malloc(strlen(s) + replacen * (nch - strlen(conf->replace_str)) + 1);
        f = s;
        char *r = rtn;
        while (true) {
            char *fn = strstr(f, conf->replace_str);
            if (fn == NULL) {
                memmove(r, f, strlen(f));
                r[strlen(f)] = 0;
                break;
            }
            memmove(r, f, fn - f);
            r += fn - f;
            memmove(r, buf1, strlen(buf1));
            r += strlen(buf1);
            f = fn + strlen(conf->replace_str);
        }
    }
    
    return rtn;
}



////////////////////////////////////////////////////////////////////////////////
// main
//

void usage(int rtn_code, char *msg)
{
    if (msg)
        fprintf(stderr, "%s\n", msg);
    fprintf(stderr, "Usage: %s [-f <liketime|rusage>] [-I <replstr>] "
      "[-i <stdincmd>] [-o <stdoutcmd>] [-q] [-s <sleep seconds>] <numruns> <command> "
      "[<arg 1> ... <arg n>]\n", __progname);
    exit(rtn_code);
}


int main(int argc, char** argv)
{
    Conf *conf = malloc(sizeof(Conf));
    conf->format_style = FORMAT_NORMAL;
    conf->input_cmd = NULL;
    conf->output_cmd = NULL;
    conf->quiet = false;
    conf->replace_str = NULL;
    conf->sleep = 3;

    int ch;
    while ((ch = getopt(argc, argv, "f:hi:I:o:qs:")) != -1) {
        switch (ch) {
            case 'f':
                if (strcmp(optarg, "liketime") == 0)
                    conf->format_style = FORMAT_LIKE_TIME;
                else if (strcmp(optarg, "rusage") == 0)
                    conf->format_style = FORMAT_RUSAGE;
                else
                    usage(1, "Unknown format style.");
                break;
            case 'h':
                usage(0, NULL);
                break;
            case 'I':
                conf->replace_str = optarg;
                break;
            case 'i':
                conf->input_cmd = optarg;
                break;
            case 'o':
                if (conf->quiet)
                    usage(1, "Can't specify output command if -q is specified.");
                conf->output_cmd = optarg;
                break;
            case 'q':
                if (conf->output_cmd)
                    usage(1, "Can't suppress stdout if -o is specified.");
                conf->quiet = true;
                break;
            case 's': {
                char *ep = optarg + strlen(optarg);
                long lval = strtoimax(optarg, &ep, 10);
                if (optarg[0] == '\0' || *ep != '\0')
                    usage(1, "'sleep' not a valid number.");
                if ((errno == ERANGE && (lval == INTMAX_MIN || lval == INTMAX_MAX))
                  || lval < 0)
                    usage(1, "'sleep' out of range.");
                conf->sleep = (int) lval;
                break;
            }
            default:
                usage(1, NULL);
                break;
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
    conf->num_runs = (int) lval;
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
    for (int i = 0; i < conf->num_runs; i += 1) {
        conf->rusages[i] = malloc(sizeof(struct rusage *) * conf->num_runs);
        memset(conf->rusages[i], 0, sizeof(struct rusage *) * conf->num_cmds);
        conf->timevals[i] = malloc(sizeof(struct timeval *) * conf->num_runs);
        memset(conf->timevals[i], 0, sizeof(struct timeval *) * conf->num_cmds);
    }

    // Seed the random number generator.

#	if defined(MT_HAVE_RANDOM) && defined(MT_HAVE_SRANDOMDEV)
	srandomdev();
#	elif defined(MT_HAVE_RANDOM)
	struct timeval tv;

	gettimeofday(&tv, NULL);
	srandom(tv.tv_sec ^ tv.tv_usec);
#	else
	struct timeval tv;

	gettimeofday(&tv, NULL);
	srand(tv.tv_sec ^ tv.tv_usec);
#	endif
    
    for (int i = 0; i < conf->num_runs; i += 1) {
        run_cmd(conf, 0, i);
        if (i + 1 < conf->num_runs && conf->sleep > 0) {
#	        ifdef MT_HAVE_RANDOM
	        usleep(random() % (conf->sleep * 1000000));
#	        else
	        usleep(rand() % (conf->sleep) * 1000000));
#	        endif
        }
    }
    
    switch (conf->format_style) {
        case FORMAT_LIKE_TIME:
            format_like_time(conf);
            break;
        case FORMAT_NORMAL:
        case FORMAT_RUSAGE:
            format_other(conf);
            break;
    }
}
