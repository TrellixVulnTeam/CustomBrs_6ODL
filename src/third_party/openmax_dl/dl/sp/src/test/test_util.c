/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "dl/sp/src/test/test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dl/sp/api/armSP.h"
#include "dl/sp/src/test/compare.h"
#include "dl/sp/src/test/gensig.h"

/*
 * Test results from running either forward or inverse FFT tests
 */
struct TestResult {
  /* Number of tests that failed */
  int failed_count_;

  /* Number of tests run */
  int test_count_;

  /* Number of tests that were expected to fail */
  int expected_failure_count_;

  /* Number of tests that were expected to fail but didn't */
  int unexpected_pass_count_;

  /* Number of tests that unexpectedly failed */
  int unexpected_failure_count_;

  /* The minimum SNR found for all of the tests */
  float min_snr_;
};

/*
 * Return the program name, fur usage messages and debugging
 */
char* ProgramName(char* argv0) {
  char* slash = strrchr(argv0, '/');

  return slash ? slash + 1 : argv0;
}

/*
 * Print usage message for the command line options.
 */
void usage(char* prog, int real_only, int max_fft_order, const char *summary) {
  fprintf(stderr, "\n%s: [-hTFI] [-n logsize] [-s scale] [-g signal-type] "
          "[-S signal value]\n\t\t[-v verbose] [-m minFFT] [-M maxFFT]\n",
          ProgramName(prog));
  fprintf(stderr, summary);
  fprintf(stderr, "  -h\t\tThis help\n");
  fprintf(stderr, "  -T\t\tIndividual test mode, otherwise run all tests\n");
  fprintf(stderr, "  -F\t\tDo not run forward FFT tests\n");
  fprintf(stderr, "  -I\t\tDo not run inverse FFT tests\n");
  fprintf(stderr, "  -m min\tMinium FFT order to test (default 2)\n");
  fprintf(stderr, "  -M min\tMaximum FFT order to test (default %d)\n",
          max_fft_order);
  fprintf(stderr, "  -n logsize\tLog2 of FFT size\n");
  fprintf(stderr, "  -s scale\tScale factor for forward FFT (default = 0)\n");
  fprintf(stderr, "  -S signal\tBase value for the test signal "
          "(default = 1024)\n");
  fprintf(stderr, "  -v level\tVerbose output level (default = 1)\n");
  fprintf(stderr, "  -g type\tInput signal type:\n");
  fprintf(stderr, "\t\t  0 - Constant signal S + i*S. (Default value.)\n");
  fprintf(stderr, "\t\t  1 - Real ramp starting at S/N, N = FFT size\n");
  fprintf(stderr, "\t\t  2 - Sine wave of amplitude S\n");
  if (!real_only)
    fprintf(stderr, "\t\t  3 - Complex signal whose transform is a sine "
            "wave.\n");
  exit(0);
}

/*
 * Set default values for all command line options.
 */
void SetDefaultOptions(struct Options* options, int real_only,
                       int max_fft_order) {
  options->real_only_ = real_only;

  options->verbose_ = 1;

  /*
   * Test mode options, defaulting to non-test mode
   */
  options->test_mode_ = 1;
  options->do_forward_tests_ = 1;
  options->do_inverse_tests_ = 1;
  options->min_fft_order_ = 1;
  options->max_fft_order_ = max_fft_order;

  /*
   * Individual test options
   */
  options->fft_log_size_ = 4;
  options->scale_factor_ = 0;
  options->signal_type_ = 0;
  options->signal_value_ = 32767;
  options->signal_value_given_ = 0;
}

/*
 * Print values of command line options, for debugging.
 */
void DumpOptions(FILE* f, const struct Options* options) {
    fprintf(f, "real_only          = %d\n", options->real_only_);
    fprintf(f, "verbose            = %d\n", options->verbose_);
    fprintf(f, "test_mode          = %d\n", options->test_mode_);
    fprintf(f, "do_forward_tests   = %d\n", options->do_forward_tests_);
    fprintf(f, "do_inverse_tests   = %d\n", options->do_inverse_tests_);
    fprintf(f, "min_fft_order      = %d\n", options->min_fft_order_);
    fprintf(f, "max_fft_order      = %d\n", options->max_fft_order_);
    fprintf(f, "fft_log_size       = %d\n", options->fft_log_size_);
    fprintf(f, "scale_factor       = %d\n", options->scale_factor_);
    fprintf(f, "signal_type        = %d\n", options->signal_type_);
    fprintf(f, "signal_value       = %g\n", options->signal_value_);
    fprintf(f, "signal_value_given = %d\n", options->signal_value_given_);
}

/*
 * Process command line options, returning the values in |options|.
 */
void ProcessCommandLine(struct Options *options, int argc, char* argv[],
                        const char* summary) {
  int opt;
  int max_fft_order = options->max_fft_order_;

  options->signal_value_given_ = 0;

  while ((opt = getopt(argc, argv, "hTFIn:s:S:g:v:m:M:")) != -1) {
    switch (opt) {
      case 'h':
        usage(argv[0], options->real_only_, max_fft_order, summary);
        break;
      case 'T':
        options->test_mode_ = 0;
        break;
      case 'F':
        options->do_forward_tests_ = 0;
        break;
      case 'I':
        options->do_inverse_tests_ = 0;
        break;
      case 'm':
        options->min_fft_order_ = atoi(optarg);
        break;
      case 'M':
        options->max_fft_order_ = atoi(optarg);
        break;
      case 'n':
        options->fft_log_size_ = atoi(optarg);
        break;
      case 'S':
        options->signal_value_ = atof(optarg);
        options->signal_value_given_ = 1;
        break;
      case 's':
        options->scale_factor_ = atoi(optarg);
        break;
      case 'g':
        options->signal_type_ = atoi(optarg);
        break;
      case 'v':
        options->verbose_ = atoi(optarg);
        break;
      default:
        usage(argv[0], options->real_only_, max_fft_order, summary);
        break;
    }
  }
}

/*
 * Return true if the given test is known to fail.  The array of known
 * failures is in |knownFailures|.  The FFT order is |fft_order|,
 * |is_inverse_fft| is true, if the test fails for the inverse FFT
 * (otherwise for forward FFT), and |signal_type| specifies the test
 * signal used.
 */
int IsKnownFailure(int fft_order, int is_inverse_fft, int signal_type,
                   struct KnownTestFailures* known_failures) {
  if (known_failures) {
    /*
     * Look through array of known failures and see if an FFT
     * (forward or inverse) of the given order and signal type
     * matches.  Return true if so.
     */
    while (known_failures->fft_order_ > 0) {
      if ((fft_order == known_failures->fft_order_)
          && (is_inverse_fft == known_failures->is_inverse_fft_test_)
          && (signal_type == known_failures->signal_type_)) {
        return 1;
      }
      ++known_failures;
    }
  }
  return 0;
}

/*
 * Run one FFT test
 */
void TestOneFFT(int fft_log_size,
                int signal_type,
                float signal_value,
                const struct TestInfo* info,
                const char* message) {
  struct SnrResult snr;

  if (info->do_forward_tests_) {
    RunOneForwardTest(fft_log_size, signal_type, signal_value, &snr);
    printf("Forward %s\n", message);
    printf("SNR:  real part    %10.3f dB\n", snr.real_snr_);
    printf("      imag part    %10.3f dB\n", snr.imag_snr_);
    printf("      complex part %10.3f dB\n", snr.complex_snr_);
  }

  if (info->do_inverse_tests_) {
    RunOneInverseTest(fft_log_size, signal_type, signal_value, &snr);
    printf("Inverse %s\n", message);
    if (info->real_only_) {
      printf("SNR:  real         %10.3f dB\n", snr.real_snr_);
    } else {
      printf("SNR:  real part    %10.3f dB\n", snr.real_snr_);
      printf("      imag part    %10.3f dB\n", snr.imag_snr_);
      printf("      complex part %10.3f dB\n", snr.complex_snr_);
    }
  }
}

/*
 * Run a set of tests, printing out the result of each test.
 */
void RunTests(struct TestResult* result,
              float (*test_function)(int, int, float, struct SnrResult*),
              const char* id,
              int is_inverse_test,
              const struct TestInfo* info,
              float snr_threshold) {
  int fft_order;
  int signal_type;
  float snr;
  int tests = 0;
  int failures = 0;
  int expected_failures = 0;
  int unexpected_failures = 0;
  int unexpected_passes = 0;
  float min_snr = 1e10;
  struct SnrResult snrResults;

  for (fft_order = info->min_fft_order_; fft_order <= info->max_fft_order_;
       ++fft_order) {
    for (signal_type = 0; signal_type < MaxSignalType(info->real_only_);
         ++signal_type) {
      int known_failure = 0;
      int test_failed = 0;
      ++tests;
      snr = test_function(fft_order, signal_type, 1024.0, &snrResults);
      if (snr < min_snr)
        min_snr = snr;
      known_failure = IsKnownFailure(fft_order, is_inverse_test,
                                     signal_type, info->known_failures_);
      if (snr < snr_threshold) {
        ++failures;
        test_failed = 1;
        if (known_failure) {
          ++expected_failures;
          printf(" *FAILED: %s ", id);
        } else {
          ++unexpected_failures;
          printf("**FAILED: %s ", id);
        }
      } else {
        test_failed = 0;
        printf("  PASSED: %s ", id);
      }
      printf("order %2d signal %d:  SNR = %9.3f",
             fft_order, signal_type, snr);
      if (known_failure) {
        if (test_failed) {
          printf(" (expected failure)");
        } else {
          ++unexpected_passes;
          printf(" (**Expected to fail, but passed)");
        }
      }
      printf("\n");
    }
  }

  printf("%sSummary:  %d %s tests failed out of %d tests. "
         "(Success rate %.2f%%.)\n",
         failures ? "**" : "",
         failures,
         id,
         tests,
         (100.0 * (tests - failures)) / tests);
  if (expected_failures || unexpected_passes || unexpected_failures) {
    printf("    (%d expected failures)\n", expected_failures);
    printf("    (%d unexpected failures)\n", unexpected_failures);
    printf("    (%d unexpected passes)\n", unexpected_passes);
  }
  
  printf("    (Minimum SNR = %.3f dB)\n", min_snr);

  result->failed_count_ = failures;
  result->test_count_ = tests;
  result->expected_failure_count_ = expected_failures;
  result->unexpected_pass_count_ = unexpected_passes;
  result->unexpected_failure_count_ = unexpected_failures;
  result->min_snr_ = min_snr;
}

/*
 * For all FFT orders and signal types, run the forward FFT.
 * runOneForwardTest must be defined to compute the forward FFT and
 * return the SNR beween the actual and expected FFT.
 *
 * Also finds the minium SNR from all of the tests and returns the
 * minimum SNR value.
 */
void RunForwardTests(struct TestResult* result, const struct TestInfo* info,
                     float snr_threshold) {
  RunTests(result, RunOneForwardTest, "FwdFFT", 0, info, snr_threshold);
}

void initializeTestResult(struct TestResult *result) {
  result->failed_count_ = 0;
  result->test_count_ = 0;
  result->expected_failure_count_ = 0;
  result->min_snr_ = 1000;
}

/*
 * For all FFT orders and signal types, run the inverse FFT.
 * runOneInverseTest must be defined to compute the forward FFT and
 * return the SNR beween the actual and expected FFT.
 *
 * Also finds the minium SNR from all of the tests and returns the
 * minimum SNR value.
 */
void RunInverseTests(struct TestResult* result, const struct TestInfo* info,
                     float snr_threshold) {
  RunTests(result, RunOneInverseTest, "InvFFT", 1, info, snr_threshold);
}

/*
 * Run all forward and inverse FFT tests, printing a summary of the
 * results.
 */
int RunAllTests(const struct TestInfo* info) {
  int failed;
  int total;
  float min_forward_snr;
  float min_inverse_snr;
  struct TestResult forward_results;
  struct TestResult inverse_results;

  initializeTestResult(&forward_results);
  initializeTestResult(&inverse_results);

  if (info->do_forward_tests_)
    RunForwardTests(&forward_results, info, info->forward_threshold_);
  if (info->do_inverse_tests_)
    RunInverseTests(&inverse_results, info, info->inverse_threshold_);

  failed = forward_results.failed_count_ + inverse_results.failed_count_;
  total = forward_results.test_count_ + inverse_results.test_count_;
  min_forward_snr = forward_results.min_snr_;
  min_inverse_snr = inverse_results.min_snr_;

  if (total) {
    printf("%sTotal: %d tests failed out of %d tests.  "
           "(Success rate = %.2f%%.)\n",
           failed ? "**" : "",
           failed,
           total,
           (100.0 * (total - failed)) / total);
    if (forward_results.expected_failure_count_
        + inverse_results.expected_failure_count_) {
      printf("  (%d expected failures)\n",
             forward_results.expected_failure_count_
             + inverse_results.expected_failure_count_);
      printf("  (%d unexpected failures)\n",
             forward_results.unexpected_failure_count_
             + inverse_results.unexpected_failure_count_);
      printf("  (%d unexpected passes)\n",
             forward_results.unexpected_pass_count_
             + inverse_results.unexpected_pass_count_);
    }
    printf("  Min forward SNR = %.3f dB, min inverse SNR = %.3f dB\n",
           min_forward_snr,
           min_inverse_snr);
  } else {
    printf("No tests run\n");
  }

  return failed;
}

/*
 * Print the contents of an array to stdout, one element per line.
 * |array_name| is the name of the array to be used in the header
 * line.
 *
 * Arrays with elements of type OMX_S16, OMX_S32, OMX_SC32, OMX_F32,
 * and OMX_FC32 are supported.
 */
void DumpArrayReal16(const char* array_name, int count,
                     const OMX_S16* array) {
  int n;

  printf("%4s\t%5s[n]\n", "n", array_name);
  for (n = 0; n < count; ++n) {
    printf("%4d\t%8d\n", n, array[n]);
  }
}

void DumpArrayReal32(const char* array_name, int count, const OMX_S32* array) {
  int n;

  printf("%4s\t%5s[n]\n", "n", array_name);
  for (n = 0; n < count; ++n) {
    printf("%4d\t%8d\n", n, array[n]);
  }
}

void DumpArrayComplex32(const char* array_name, int count,
                        const OMX_SC32* array) {
  int n;

  printf("%4s\t%10s.re[n]\t%10s.im[n]\n", "n", array_name);
  for (n = 0; n < count; ++n) {
    printf("%4d\t%16d\t%16d\n", n, array[n].Re, array[n].Im);
  }
}

void DumpArrayComplex16(const char* array_name, int count,
                        const OMX_SC16* array) {
  int n;

  printf("%4s\t%10s.re[n]\t%10s.im[n]\n", "n", array_name, array_name);
  for (n = 0; n < count; ++n) {
    printf("%4d\t%16d\t%16d\n", n, array[n].Re, array[n].Im);
  }
}

void DumpArrayFloat(const char* array_name, int count, const OMX_F32* array) {
  int n;

  printf("%4s\t%13s[n]\n", "n", array_name);
  for (n = 0; n < count; ++n) {
    printf("%4d\t%16g\n", n, array[n]);
  }
}

void DumpArrayComplexFloat(const char* array_name, int count,
                           const OMX_FC32* array) {
  int n;

  printf("%4s\t%10s.re[n]\t%10s.im[n]\n", "n", array_name, array_name);
  for (n = 0; n < count; ++n) {
    printf("%4d\t%16g\t%16g\n", n, array[n].Re, array[n].Im);
  }
}
