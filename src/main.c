#include "test_framework/test_config.h"
#include "core/globals.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_THREADS     10
#define DEFAULT_CORES       4
#define DEFAULT_PERCENT_OP  50
#define DEFAULT_PERCENT_EX  50
#define DEFAULT_OP_COUNT    32768
#define DEFAULT_LESS_OP     32
#define DEFAULT_OP_BETWEEN  500
#define DEFAULT_TIME        5000  // in milliseconds

void print_help(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -t <threads>    Set number of threads (default: %d)\n", DEFAULT_THREADS);
    printf("  -T <threads>    Set maximum number of threads (default: %d)\n", DEFAULT_THREADS);
    printf("  -c <cores>      Set available cores (default: %d)\n", DEFAULT_CORES);
    printf("  -p <percent>    Set percentage of operations (insert/delete) (default: %d)\n", DEFAULT_PERCENT_OP);
    printf("  -e <percent>    Set percentage of extractions (deletemin/deletemax) (default: %d)\n", DEFAULT_PERCENT_EX);
    printf("  -i <test_id>    Run specific test (default: run all tests)\n");
    printf("  -o <op_count>   Set operation count (default: %u)\n", DEFAULT_OP_COUNT);
    printf("  -l <less_op>    Set less operations threshold (default: %u)\n", DEFAULT_LESS_OP);
    printf("  -b <op_between> Set operations between checks (default: %u)\n", DEFAULT_OP_BETWEEN);
    printf("  -m <ms>         Set time limit for main time operations (default: %u ms)\n", DEFAULT_TIME);
    printf("  -s <ms>         Set time limit for secondary time operations (default: %u ms)\n", DEFAULT_TIME);
    printf("  -h              Show this help message\n");
}

int main(int argc, char **argv) {
    numberOfThreads = DEFAULT_THREADS;
    availableCores = DEFAULT_CORES;
    unsigned int percent_op = DEFAULT_PERCENT_OP;
    unsigned int percent_ex = DEFAULT_PERCENT_EX;
    op_count = DEFAULT_OP_COUNT;
    less_op = DEFAULT_LESS_OP;
    op_between_check = DEFAULT_OP_BETWEEN;
    ms_main = DEFAULT_TIME;
    ms_secondary = DEFAULT_TIME;
    int test_id = -1;

    int opt;
    while ((opt = getopt(argc, argv, "t:T:c:p:e:i:o:l:b:m:s:h")) != -1) {
        switch (opt) {
            case 't':
                numberOfThreads = atoi(optarg);
                break;
            case 'T':
                maxThreads = atoi(optarg);
                break;
            case 'c':
                availableCores = atoi(optarg);
                break;
            case 'p':
                percent_op = atoi(optarg);
                break;
            case 'e':
                percent_ex = atoi(optarg);
                break;
            case 'i':
                test_id = atoi(optarg);
                break;
            case 'o':
                op_count = atoi(optarg);
                break;
            case 'l':
                less_op = atoi(optarg);
                break;
            case 'b':
                op_between_check = atoi(optarg);
                break;
            case 'm':
                ms_main = atoi(optarg);
                break;
            case 's':
                ms_secondary = atoi(optarg);
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                fprintf(stderr, "Unknown option or missing argument. Use -h for help.\n");
                return 1;
        }
    }

    if (maxThreads < numberOfThreads) {
        fprintf(stderr, "Maximum number of threads (%d) should be higher or equal to Number of Threads (%d).\n", maxThreads, numberOfThreads);
        return 1;
    }
    if (test_id < 0 || test_id >= NUM_TEST_TYPE) {
        fprintf(stderr, "Invalid test ID. Must be between 0 and %d\n", NUM_TEST_TYPE - 1);
        return 1;
    }

    printf("Test ID: %d\n", test_id);

    srand(time(NULL));

    run_test((Test) test_id, percent_op, percent_ex, numberOfThreads);

    printStatistics();

    return 0;
}