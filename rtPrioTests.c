/*****************************************************************************
 *
 * rtPrioTests.c
 *
 * Example of the effects of realtime scheduling policy on process execution.
 * Call gettimeofday() repeatedly to demonstrate the effects of paging with
 * different scheduling polices and process priorities.
 *
 * Original code by Shawn Quinn
 * Created Date:  12/18/2014
 *
 ****************************************************************************/

#include <sys/time.h>
#include <stdio.h>
// add the following to allow changing the default scheduler policy
#include <sched.h>

//#define MY_RT_PRIORITY 0 /* Lowest possible */
#define MY_RT_PRIORITY 99 /* Highest possible */
#define RUN_AS_RT

const int delVal = 25000;	// 25 msec delay parameter
const int buffSize = 40000; // big enough to force paging

int main(void)
{
#ifdef RUN_AS_RT
	int rc, old_scheduler_policy;
	struct sched_param my_params;
	// Passing zero specifies caller’s (our) policy
	old_scheduler_policy = sched_getscheduler(0);
	my_params.sched_priority = MY_RT_PRIORITY;
	// Passing zero specifies callers (our) pid
	rc = sched_setscheduler(0, SCHED_FIFO, &my_params);
	if ( rc == -1 )
		printf("could not change scheduler policy\n");
#endif
    int i = 0;
    int j = 0;
    int dummyBuff[buffSize];
    struct timeval tv1, tv2, tvdel;
    tvdel.tv_sec = 0;
    tvdel.tv_usec = delVal;
    for(i = 0; i < 100; ++i)
    {
        gettimeofday(&tv1, NULL);
        for(j = 0; j < buffSize; ++j)
        {
            dummyBuff[j] = 0;   // give process something to chew on
        }
        select(0, NULL, NULL, NULL, &tvdel);	// delay...
        gettimeofday(&tv2, NULL);
        printf("first time value = %d\n", (int)tv1.tv_usec);
        printf("second time value = %d\n", (int)tv2.tv_usec);
        printf("delay (msec) = %d\n", (int)(tv2.tv_usec - tv1.tv_usec) - delVal);
        tvdel.tv_sec = 0;
        tvdel.tv_usec = delVal;
    }

    return 0;
}
