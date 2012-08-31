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


enum Format_Style {FORMAT_LIKE_TIME, FORMAT_NORMAL, FORMAT_RUSAGE};

typedef struct {
    enum Format_Style format_style;
    const char *input_cmd;
    const char *output_cmd;
    bool quiet;                 // True = suppress command's stdout.
    const char *replace_str;
    int sleep;                  // Time to sleep between commands, in seconds.
                                // 0 = no sleep.
    
    int num_cmds;               // How many commands the user has specified.
    char ***cmds;               // The pre-processed execvp'able arguments for
                                // each command.
    struct timeval ***timevals; // The wall clock time for each command run.
    struct rusage ***rusages;   // The rusage each command run.
    int num_runs;               // How many times to run each command.
} Conf;
