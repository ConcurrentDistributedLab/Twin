#ifndef TIMER_H
#define TIMER_H

#include <time.h>

typedef struct Timer
{
    struct timespec start;
    struct timespec end;
} Timer;

void startTimer(Timer *timer);
void stopTimer(Timer *timer);
double elapsedTimeSeconds(Timer *timer);
double elapsedTimeMilliSeconds(Timer *timer);
double getCurrentElapsedSeconds(Timer *timer);
double getCurrentElapsedMilliseconds(Timer *timer);

#endif