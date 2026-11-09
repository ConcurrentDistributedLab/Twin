#include "timer.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

void startTimer(Timer *timer)
{
    clock_gettime(CLOCK_MONOTONIC, &timer->start);
}

void stopTimer(Timer *timer)
{
    clock_gettime(CLOCK_MONOTONIC, &timer->end);
}

double elapsedTimeSeconds(Timer *timer)
{
    double start_sec = timer->start.tv_sec + (timer->start.tv_nsec / 1e9);
    double end_sec = timer->end.tv_sec + (timer->end.tv_nsec / 1e9);
    return end_sec - start_sec;
}

double elapsedTimeMilliSeconds(Timer *timer)
{
    double start_ms = (timer->start.tv_sec * 1000.0) + (timer->start.tv_nsec / 1e6);
    double end_ms = (timer->end.tv_sec * 1000.0) + (timer->end.tv_nsec / 1e6);
    return end_ms - start_ms;
}

double getCurrentElapsedSeconds(Timer *timer)
{
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);

    double start_sec = timer->start.tv_sec + (timer->start.tv_nsec / 1e9);
    double current_sec = current.tv_sec + (current.tv_nsec / 1e9);

    return current_sec - start_sec;
}

double getCurrentElapsedMilliseconds(Timer *timer)
{
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);

    double start_ms = (timer->start.tv_sec * 1000.0) + (timer->start.tv_nsec / 1e6);
    double current_ms = (current.tv_sec * 1000.0) + (current.tv_nsec / 1e6);

    return current_ms - start_ms;
}