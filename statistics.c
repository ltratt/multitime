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

#include <math.h>

#include "statistics.h"
#include "tvals.h"
#include "zvals.h"


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
