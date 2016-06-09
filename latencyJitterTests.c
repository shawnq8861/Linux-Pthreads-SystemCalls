/*****************************************************************************
 *
 * latencyJitterTests.c
 *
 * Example of the effects of realtime scheduling policy and memory locking
 * on latency variation or jitter.
 *
 * Original code by Shawn Quinn
 * Created Date:  01/05/2015
 *
 ****************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>

#define WAIT 50     // for a 50 millisecond pause
const char rtEnable[] = "high";
//const char rtEnable[] = "low";

int main(void)
{
	int result = 0;
	int count = 0;
	long min, max, average, current, interval;
	struct timeval cur_time, last_time;
	struct sched_param mysched;
	fd_set inputs, testfds;

    if(strncmp(rtEnable, "high", 4) == 0)
    {
    	//set scheduler policy to SCHED_FIFO, which will inhibit preemption, with
    	//the maximum priority, 99
        mysched.sched_priority = 99;
        if(sched_setscheduler (0, SCHED_FIFO, &mysched) == -1)
        {
            puts(" ERROR IN SETTING THE SCHEDULER UP");
            perror("errno");
            exit (0);
        }
        // lock memory to prevent paging
        mlockall(MCL_CURRENT | MCL_FUTURE);
        printf("Using high priority\n");
    }
/*
    Initialize stuff
*/
	FD_ZERO (&inputs);      // select data structure
	FD_SET (0, &inputs);
	
	max = 0;
	average = 0;
	min = 1000000;


    gettimeofday(&last_time, NULL);

    while(result == 0)
    {
    	++count;
 /* We use select() to generate a sub-second timeout and also
    detect when the user wants to stop.  Note that both cur_time
    and testfds can be changed by select().
 */
        cur_time.tv_sec = 0;
        cur_time.tv_usec = WAIT*1000;
        testfds = inputs;
        result = select(FD_SETSIZE, &testfds, NULL, NULL, &cur_time);
 /*
    Get the current time, compute the interval in microseconds from
    the last loop and compute the deviation from what we expect.
 */
        gettimeofday(&cur_time, NULL);
        interval = (cur_time.tv_sec - last_time.tv_sec)*1000000 +
                   (cur_time.tv_usec - last_time.tv_usec);
        current = interval - WAIT*1000;
 /*
    Update the statistics and print them
 */
        if(abs (current) > max)
            max = abs (current);
        if(abs (current) < min)
            min = abs (current);
        if(average == 0)
            average = abs (current);
        else
            average = (average + abs (current))/2;
        last_time = cur_time;
        if(result == 0)
            printf("min %ld, max %ld, avg %ld, current %ld          \r",
                     min, max, average, current);
        last_time = cur_time;
    }
	printf("\nEnd latency test process, iteration count = %d\n", count);
	return 0;
}
