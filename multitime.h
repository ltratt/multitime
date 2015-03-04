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


enum Format_Style {FORMAT_UNKNOWN, FORMAT_LIKE_TIME, FORMAT_NORMAL, FORMAT_RUSAGE};

typedef struct {
    char ** argv;
    const char *pre_cmd;
    const char *input_cmd;
    const char *output_cmd;
    const char *replace_str;
    bool quiet_stdout;         // True = suppress command's stdout.
    bool quiet_stderr;         // True = suppress command's stderr.
    struct timeval **timevals; // The wall clock time for each command run.
    struct rusage **rusages;   // The rusage each command run.
} Cmd;

typedef struct {
    Cmd **cmds;
    int num_cmds;               // How many commands the user has specified.
    int num_runs;               // How many times to run each command.
    int conf_level;             // Confidence level (as a percentage, e.g. 95).

    enum Format_Style format_style;
    int sleep;                  // Time to sleep between commands, in seconds.
                                // 0 = no sleep.
    int verbosity;              // 0 to +ve: higher values may increase
                                // verbosity.
} Conf;
