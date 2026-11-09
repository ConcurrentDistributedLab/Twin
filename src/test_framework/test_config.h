#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

typedef enum {
    // Time-based insert tests
    TEST_INSERT_INCREASE_TIME,
    TEST_INSERT_RANDOM_TIME,

    // Fixed-op deletes after random inserts
    TEST_DELETE_RANDOM,

    // Timed mixed insert/delete
    TEST_RANDOM_WITH_ELEMENTS_RK_TIME,

    NUM_TEST_TYPE  // Keep this last to count number of tests
} Test;

void run_test(Test type, unsigned int percent_op, unsigned int percent_ex, int numberOfThreads);

#endif
