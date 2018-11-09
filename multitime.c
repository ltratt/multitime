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
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "multitime.h"
#include "format.h"



#define BUFFER_SIZE (64 * 1024)


extern char* __progname;

void usage(int, char *);
void execute_cmd(Conf *, Cmd *, int);
FILE *read_input(Conf *, Cmd *, int);
bool fcopy(FILE *, FILE *);
char *replace(Conf *, Cmd *, const char *, int);
char escape_char(char);

#if defined(MT_HAVE_ARC4RANDOM)
#define RANDN(n) (arc4random() % n)
#elif defined(MT_HAVE_DRAND48)
#define RANDN(n) ((int) (drand48() * n))
#elif defined(MT_HAVE_RANDOM)
#define RANDN(n) (random() % n)
#else
#define RANDN(n) (rand() % n)
#endif



////////////////////////////////////////////////////////////////////////////////
// Running commands
//

#include <fcntl.h>

void execute_cmd(Conf *conf, Cmd *cmd, int runi)
{
    if (conf->verbosity > 0) {
        fprintf(stderr, "===> Executing ");
        pp_cmd(conf, cmd);
        fprintf(stderr, "\n");
    }

    if (cmd->pre_cmd) {
        char *pre_cmd = replace(conf, cmd, cmd->pre_cmd, runi);
        if (system(pre_cmd) != 0)
            errx(1, "Exiting because '%s' failed.", pre_cmd);
        free(pre_cmd);
    }

    FILE *tmpf = NULL;
    if (cmd->input_cmd)
        tmpf = read_input(conf, cmd, runi);

    FILE *outtmpf = NULL;
    char *output_cmd = replace(conf, cmd, cmd->output_cmd, runi);
    if (output_cmd) {
        char outtmpp[] = "/tmp/mt.XXXXXXXXXX";
        umask(S_IRWXG | S_IRWXO | S_IXUSR);
        int outtmpfd = mkstemp(outtmpp);
        if (outtmpfd != -1)
            outtmpf = fdopen(outtmpfd, "r+");
        if (outtmpfd == -1 || outtmpf == NULL)
            errx(1, "Can't create temporary file.");
    }

    struct rusage *ru = cmd->rusages[runi] =
      malloc(sizeof(struct rusage));

    // Note: we want to do as little stuff in either parent or child between the
    // two gettimeofday calls, otherwise we might interfere with the timings.

    struct timeval startt;
    gettimeofday(&startt, NULL);
    pid_t pid = fork();
    if (pid == 0) {
        // Child. Note we don't deal with errors directly here, but simply report
        // them back to the parent, which will then exit.
        if (tmpf && dup2(fileno(tmpf), STDIN_FILENO) == -1)
            exit(1);

        if (cmd->quiet_stdout && freopen("/dev/null", "w", stdout) == NULL)
            exit(1);
        if (cmd->quiet_stderr && freopen("/dev/null", "w", stderr) == NULL)
            exit(1);
        else if (output_cmd && dup2(fileno(outtmpf), STDOUT_FILENO) == -1)
            exit(1);
        execvp(cmd->argv[0], cmd->argv);
        exit(1);
    }

    // Parent

    int status;
    wait4(pid, &status, 0, ru);
    struct timeval endt;
    gettimeofday(&endt, NULL);

    if (status != 0)
        errx(status, "Error when attempting to run %s", cmd->argv[0]);

    if (tmpf)
        fclose(tmpf);

    struct timeval *tv = cmd->timevals[runi] = malloc(sizeof(struct timeval));
    timersub(&endt, &startt, tv);

    // If an output command is specified, pipe the temporary output to it, and
    // check its return code.

    if (output_cmd) {
        fflush(outtmpf);
        fseek(outtmpf, 0, SEEK_SET);
        FILE *cmdf = popen(output_cmd, "w");
        if (cmdf == NULL || !fcopy(outtmpf, cmdf))
            errx(1, "Error when attempting to run %s", output_cmd);
        if (pclose(cmdf) != 0)
            errx(1, "Exiting because '%s' failed.", output_cmd);
        fclose(outtmpf);
        free(output_cmd);
    }

    return;
}



//
// Read in the input from cmd->input_cmd for runi and return an open file set
// to read from the beginning which contains its output.
//

FILE *read_input(Conf *conf, Cmd *cmd, int runi)
{
    assert(cmd->input_cmd);

    char *input_cmd = replace(conf, cmd, cmd->input_cmd, runi);
    FILE *cmdf = popen(input_cmd, "r");
    if (!cmdf)
        goto cmd_err;
    char tmpp[] = "/tmp/mt.XXXXXXXXXX";
    umask(S_IRWXG | S_IRWXO | S_IXUSR);
    int tmpfd = mkstemp(tmpp);
    if (tmpfd == -1)
        goto cmd_err;
    FILE *tmpf = fdopen(tmpfd, "r+");
    if (!tmpf)
        goto cmd_err;

    fcopy(cmdf, tmpf);
    if (pclose(cmdf) != 0)
        goto cmd_err;
    free(input_cmd);
    fseek(tmpf, 0, SEEK_SET);

    return tmpf;

cmd_err:
    errx(1, "Error when attempting to run %s.", cmd->input_cmd);
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
// Take in string 's' and replace all instances of cmd->replace_str with
// str(runi + 1). Always returns a malloc'd string (even if cmd->replace_str is
// not in s) which must be manually freed *except* if s is NULL, whereupon NULL
// is returned.
//

char *replace(Conf *conf, Cmd *cmd, const char *s, int runi)
{
    if (s == NULL)
        return NULL;

    char *rtn;
    if (!cmd->replace_str) {
        rtn = malloc(strlen(s) + 1);
        memmove(rtn, s, strlen(s));
        rtn[strlen(s)] = 0;
    }
    else {
        int replacen = 0;
        const char *f = s;
        while (true) {
            f = strstr(f, cmd->replace_str);
            if (f == NULL)
                break;
            replacen++;
            f += strlen(cmd->replace_str);
        }
        int nch = snprintf(NULL, 0, "%d", runi + 1);
        char buf1[nch + 1];
        snprintf(buf1, nch + 1, "%d", runi + 1);
        rtn = malloc(strlen(s) + replacen * (nch - strlen(cmd->replace_str)) + 1);
        f = s;
        char *r = rtn;
        while (true) {
            char *fn = strstr(f, cmd->replace_str);
            if (fn == NULL) {
                memmove(r, f, strlen(f));
                r[strlen(f)] = 0;
                break;
            }
            memmove(r, f, fn - f);
            r += fn - f;
            memmove(r, buf1, strlen(buf1));
            r += strlen(buf1);
            f = fn + strlen(cmd->replace_str);
        }
    }

    return rtn;
}



////////////////////////////////////////////////////////////////////////////////
// Start-up routines
//

//
// Parse a batch file and update conf accordingly. This is fairly simplistic,
// and will probably never match any specific shell but hopefully does a
// sensible enough job on the expected lowest common denominator.
//

void parse_batch(Conf *conf, char *path)
{
    FILE *bf = fopen(path, "r");
    if (bf == NULL)
        err(1, "Error when trying to open '%s'", path);
    struct stat sb;
    if (fstat(fileno(bf), &sb) == -1)
        err(1, "Error when trying to fstat '%s'", path);
    size_t bfsz = sb.st_size;
    char *bd = malloc(bfsz);
    if (fread(bd, 1, bfsz, bf) < sb.st_size)
        err(1, "Error when trying to read from '%s'", path);
    fclose(bf);

    int num_cmds = 0;
    Cmd **cmds = malloc(sizeof(Cmd *));
    off_t i = 0;
    int lineno = 1;
    while (i < bfsz) {
        // Skip space at beginning of line.
        while (i < bfsz && (bd[i] == ' ' || bd[i] == '\t'))
            i += 1;
        if (i == bfsz)
            break;
        if (bd[i] == '\n' || bd[i] == '\r') {
            if (bd[i] == '\n')
                lineno += 1;
            i += 1;
            continue;
        }
        // Skip comment lines.
        if (bd[i] == '#') {
            i += 1;
            while (i < bfsz && bd[i] != '\n' && bd[i] != '\r')
                i += 1;
            if (i == bfsz)
                break;
            continue;
        }

        int argc = 0;
        char **argv = malloc(sizeof(char *));
        while (i < bfsz && bd[i] != '\n' && bd[i] != '\r') {
            int j = i;
            // Skip whitespace at the beginning of lines, as well as complete blank lines
            while (j < bfsz && (bd[j] == ' ' || bd[j] == '\t' || bd[j] == '\r'))
                j += 1;
            if (j > i) {
                i = j;
                continue;
            }

            // Allow logical lines to split over multiple physical lines.
            if (bd[i] == '\\' && i + 1 < bfsz
              && (bd[i + 1] == '\n' || bd[i + 1] == '\r')) {
                i += 1;
                while (i < bfsz && (bd[i] == '\n' || bd[i] == '\r')) {
                    if (bd[i] == '\n')
                        lineno += 1;
                    i += 1;
                }
                continue;
            }

            char *arg;
            char qc = 0;
            if (bd[i] == '"' || bd[i] == '\'') {
                qc = bd[i];
                i += 1;
            }
            // Work out the length of the arg. Note that two-character
            // pairs '\x' are length 2 in the input, but only 1 character
            // in the arg.
            j = i;
            size_t argsz = 0;
            while (j < bfsz) {
                if (qc && bd[j] == qc)
                    break;
                else if (bd[j] == '\n' || bd[j] == '\r') {
                    if (qc)
                        errx(1, "Unterminated string at line %d.", lineno);
                    else
                        break;
                }
                else if (!qc && bd[j] == ' ') {
                    break;
                }
                else if (bd[j] == '\\') {
                    if (j + 1 == bfsz)
                        errx(1, "Escape char not specified line %d.", lineno);
                    if (bd[j + 1] == '\n' || bd[j + 1] == '\r') {
                        if (qc) {
                            errx(1,
                              "'\' ambiguous before a newline in strings %d.",
                              lineno);
                        }
                        break;
                    }
                    argsz += 1;
                    j += 2;
                }
                else {
                    argsz += 1;
                    j += 1;
                }
            }
            if (qc && j == bfsz)
                errx(1, "Unterminated string at line %d.", lineno);
            arg = malloc(argsz + 1);
            if (arg == NULL)
                errx(1, "Out of memory.");
            // Copy the arg.
            j = 0;
            while (i < bfsz) {
                if (qc && bd[i] == qc) {
                    i += 1;
                    break;
                }
                else if (bd[i] == '\n' || bd[i] == '\r') {
                    assert(!qc);
                    break;
                }
                else if (!qc && bd[i] == ' ')
                    break;
                else if (bd[i] == '\\') {
                    assert(i + 1 < bfsz);
                    if (bd[j + 1] == '\n' || bd[j + 1] == '\r') {
                        assert(!qc);
                        break;
                    }
                    assert(j < argsz);
                    arg[j] = escape_char(bd[i + 1]);
                    i += 2;
                    j += 1;
                }
                else {
                    assert(j < argsz);
                    arg[j] = bd[i];
                    i += 1;
                    j += 1;
                }
            }
            assert(j == argsz);
            arg[j] = 0;

            argc += 1;
            argv = realloc(argv, argc * sizeof(char *));
            if (argv == NULL)
                errx(1, "Out of memory.");
            argv[argc - 1] = arg;
        }

        Cmd *cmd = malloc(sizeof(Cmd));
        cmd->pre_cmd = cmd->input_cmd = cmd->output_cmd = cmd->replace_str = NULL;
        cmd->quiet_stdout = cmd->quiet_stderr = false;
        cmd->rusages = malloc(sizeof(struct rusage *) * conf->num_runs);
        cmd->timevals = malloc(sizeof(struct timeval *) * conf->num_runs);
        memset(cmd->rusages, 0, sizeof(struct rusage *) * conf->num_runs);
        memset(cmd->timevals, 0, sizeof(struct rusage *) * conf->num_runs);
        int j = 0;
        while (j < argc) {
            if (strcmp(argv[j], "-I") == 0) {
                if (j + 1 == argc)
                    errx(1, "option requires an argument -- I at line %d", lineno);
                cmd->replace_str = argv[j + 1];
                free(argv[j]);
                j += 2;
            }
            else if (strcmp(argv[j], "-i") == 0) {
                if (j + 1 == argc)
                    errx(1, "option requires an argument -- i at line %d", lineno);
                cmd->input_cmd = argv[j + 1];
                free(argv[j]);
                j += 2;
            }
            else if (strcmp(argv[j], "-o") == 0) {
                if (j + 1 == argc)
                    errx(1, "option requires an argument -- o at line %d", lineno);
                cmd->output_cmd = argv[j + 1];
                free(argv[j]);
                j += 2;
            }
            else if (strcmp(argv[j], "-q") == 0) {
                if (cmd->quiet_stdout)
                    cmd->quiet_stderr = true;
                else
                    cmd->quiet_stdout = true;
                j += 1;
            }
            else if (strcmp(argv[j], "-r") == 0) {
                if (j + 1 == argc)
                    errx(1, "option requires an argument -- r at line %d", lineno);
                cmd->pre_cmd = argv[j + 1];
                free(argv[j]);
                j += 2;
            }
            else if (strlen(argv[j]) > 0 && argv[j][0] == '-') {
                if (strlen(argv[j]) == 1)
                    errx(1, "option name not given -- at line %d", lineno);
                else
                    errx(1, "unknown option -- %c at line %d", argv[j][1], lineno);
            }
            else
                break;
        }
        char **new_argv = malloc((argc - j + 1) * sizeof(char *));
        memmove(new_argv, argv + j, (argc - j) * sizeof(char *));
        free(argv);
        argc -= j;
        new_argv[argc] = NULL;
        cmd->argv = new_argv;
        num_cmds += 1;
        cmds = realloc(cmds, num_cmds * sizeof(Cmd *));
        if (cmds == NULL)
            errx(1, "Out of memory.");
        cmds[num_cmds - 1] = cmd;
    }

    free(bd);
    conf->cmds = cmds;
    conf->num_cmds = num_cmds;

    return;
}



//
// Given a char c, assuming it was prefixed by '\' (e.g. '\r'), return the
// escaped code.
//

char escape_char(char c)
{
    switch (c) {
        case '0':
            return '\0';
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\r';
        default:
            return c;
    }
}



void usage(int rtn_code, char *msg)
{
    if (msg)
        fprintf(stderr, "%s\n", msg);
    fprintf(stderr, "Usage:\n  %s [-c <level>] [-f <liketime|rusage>] [-I <replstr>]\n"
      "    [-i <stdincmd>] [-n <numruns> [-o <stdoutcmd>] [-q] [-s <sleep>]\n"
      "    <command> [<arg 1> ... <arg n>]\n"
      "  %s -b <file> [-c <level>] [-f <rusage>] [-s <sleep>]\n"
      "    [-n <numruns>]\n", __progname, __progname);
    exit(rtn_code);
}



int main(int argc, char** argv)
{
    Conf *conf = malloc(sizeof(Conf));
    conf->num_runs = 1;
    conf->format_style = FORMAT_UNKNOWN;
    conf->sleep = 3;
    conf->verbosity = 0;
    conf->conf_level = 99;

    bool quiet_stdout = false, quiet_stderr = false;
    char *batch_file = NULL;
    char *pre_cmd = NULL, *input_cmd = NULL, *output_cmd = NULL, *replace_str = NULL;
    int ch;
    while ((ch = getopt(argc, argv, "+b:c:f:hi:ln:I:o:pqr:s:v")) != -1) {
        switch (ch) {
            case 'b':
                batch_file = optarg;
                break;
            case 'c': {
                char *ep = optarg + strlen(optarg);
                long lval = (int)strtoimax(optarg, &ep, 10);
                if (optarg[0] == '\0' || *ep != '\0')
                    usage(1, "'level' not a valid number.");
                if ((errno == ERANGE && (lval == INTMAX_MIN || lval == INTMAX_MAX))
                  || lval < 1 || lval > 99)
                    usage(1, "'level' out of range.");
                conf->conf_level = (int) lval;
                break;
            }
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
                replace_str = optarg;
                break;
            case 'i':
                input_cmd = optarg;
                break;
            case 'l':
                conf->format_style = FORMAT_RUSAGE;
                break;
            case 'n':
                errno = 0;
                char *ep = optarg + strlen(optarg);
                long lval = strtoimax(optarg, &ep, 10);
                if (optarg[0] == 0 || *ep != 0)
                    usage(1, "'num runs' not a valid number.");
                if ((errno == ERANGE && (lval == INTMAX_MIN || lval == INTMAX_MAX))
                  || lval <= 0 || lval >= UINT_MAX)
                    usage(1, "'num runs' out of range.");
                conf->num_runs = (int) lval;
                break;
            case 'o':
                output_cmd = optarg;
                break;
            case 'p':
                conf->format_style = FORMAT_LIKE_TIME;
                break;
            case 'q':
                if (quiet_stdout)
                    quiet_stderr = true;
                else
                    quiet_stdout = true;
                break;
            case 'r':
                pre_cmd = optarg;
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
            case 'v':
                conf->verbosity += 1;
                break;
            default:
                usage(1, NULL);
                break;
        }
    }
    argc -= optind;
    argv += optind;

    if (batch_file && conf->format_style == FORMAT_LIKE_TIME)
        usage(1, "Can't use batch file mode with -f liketime.");
    if (batch_file && (input_cmd || output_cmd || replace_str || quiet_stdout))
        usage(1, "In batch file mode, -I/-i/-o/-q must be specified per-command in the batch file.");
    if (quiet_stdout && output_cmd)
        usage(1, "-q and -o are mutually exclusive.");

    if (conf->format_style == FORMAT_UNKNOWN) {
        if (strcmp(__progname, "time") == 0)
            conf->format_style = FORMAT_LIKE_TIME;
        else
            conf->format_style = FORMAT_NORMAL;
    }

    // Process the command(s).

    if (batch_file) {
        // Batch file mode.

        parse_batch(conf, batch_file);
    }
    else {
        // Simple mode: one command specified on the command-line.

        if (argc == 0)
            usage(1, "Missing command.");

        Cmd *cmd;
        if ((conf->cmds = malloc(sizeof(Cmd *))) == NULL
          || (cmd = malloc(sizeof(Cmd))) == NULL)
            errx(1, "Out of memory.");
        conf->num_cmds = 1;
        conf->cmds[0] = cmd;
        cmd->argv = argv;
        cmd->pre_cmd = pre_cmd;
        cmd->input_cmd = input_cmd;
        cmd->output_cmd = output_cmd;
        cmd->replace_str = replace_str;
        cmd->quiet_stdout = quiet_stdout;
        cmd->quiet_stderr = quiet_stderr;
        cmd->rusages = malloc(sizeof(struct rusage *) * conf->num_runs);
        cmd->timevals = malloc(sizeof(struct timeval *) * conf->num_runs);
        memset(cmd->rusages, 0, sizeof(struct rusage *) * conf->num_runs);
        memset(cmd->timevals, 0, sizeof(struct rusage *) * conf->num_runs);
    }

    // Seed the random number generator.

#   if defined(MT_HAVE_ARC4RANDOM)
    // arc4random doesn't need to be seeded
#	elif defined(MT_HAVE_DRAND48)
	struct timeval tv;

	gettimeofday(&tv, NULL);
	srand48(tv.tv_sec ^ tv.tv_usec);
#	elif defined(MT_HAVE_RANDOM) && defined(MT_HAVE_SRANDOMDEV)
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

    for (int i = 0; i < (conf->num_cmds * conf->num_runs); i += 1) {
        // Find a command which has not yet had all its runs executed.
        Cmd *cmd;
        while (true) {
            cmd = conf->cmds[RANDN(conf->num_cmds)];
            int j;
            for (j = 0; j < conf->num_runs; j += 1) {
                if (cmd->rusages[j] == NULL)
                    break;
            }
            if (j < conf->num_runs)
                break;
        }

        // Find a run of cmd which has not yet been executed.
        int runi;
        while (true) {
            runi = RANDN(conf->num_runs);
            if (cmd->rusages[runi] == NULL)
                break;
        }

        // Execute the command and, if there are more commands yet to be run,
        // sleep.
        execute_cmd(conf, cmd, runi);
        if (i + 1 < conf->num_runs && conf->sleep > 0)
	        usleep(RANDN(conf->sleep * 1000000));
    }

    if (conf->format_style == FORMAT_LIKE_TIME)
        format_like_time(conf);
    else
        format_other(conf);

    free(conf);
}
