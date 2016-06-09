/*****************************************************************************
 *
 * sysCallsCPUSched.c
 *
 * Example of using linux system calls, sched_getaffinity and sched_setaffinity.
 * Additionally demonstrates use of cpu_set masks and MACROS.
 *
 * Original code by Shawn Quinn
 * Created Date:  01/05/2015
 *
 ****************************************************************************/

#define _GNU_SOURCE
#include <sys/time.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>

int main(void)
{
	int retVal;				// to be used with system calls
	pid_t currPid;
	cpu_set_t cpuSet;
	CPU_ZERO(&cpuSet);		// zero out all bits in mask
	int cpu = CPU_ISSET(0, &cpuSet);
	char* setStr = cpu ? "set" : "not set";
	printf("\ninitially:  CPU 0 is %s in the mask\n", setStr);
	CPU_SET(0, &cpuSet);	// set bit for processor 0
	cpu = CPU_ISSET(0, &cpuSet);
	setStr = cpu ? "set" : "not set";
	printf("after setting with CPU_SET:  CPU 0 is %s in the mask\n", setStr);
	CPU_ZERO(&cpuSet);		// zero all bits again
	cpu = CPU_ISSET(0, &cpuSet);
	setStr = cpu ? "set" : "not set";
	printf("after clearing:  CPU 0 is %s in the mask\n", setStr);
	printf("\nThis demonstrates ability to set and clear the mask...\n\n");
	currPid = getpid();
	// the following call to sched_getaffinity writes the hard processor
	// affinity to the cpu_set_t variable passed to it
	retVal = sched_getaffinity(currPid, sizeof(cpu_set_t), &cpuSet);
	if ( retVal != 0 ) {
		printf("could not get processor affinity...\n");
		return -1;
	}
	printf("\nafter calling sched_getaffinity...\n\n");
	int i;
	for(i = 0; i < 5; ++i) {
		cpu = CPU_ISSET(i, &cpuSet);
		setStr = cpu ? "set" : "not set";
		printf("CPU %d is %s in hard affinity\n", i, setStr);
	}
	// set processor 1 and call sched_setaffinity to restrict this process
	// to processor 1
	printf("\nzeroing the mask...\n\n");
	CPU_ZERO(&cpuSet);		// zero out all bits in mask
	printf("\nsetting processor 1 with CPU_SET...\n\n");
	CPU_SET(1, &cpuSet);	// set bit for processor 1
	printf("\ncalling sched_setaffinity()...\n\n");
	retVal = sched_setaffinity(currPid, sizeof(cpu_set_t), &cpuSet);
	if ( retVal != 0 ) {
		printf("could not set processor affinity...\n");
		return -1;
	}
	CPU_ZERO(&cpuSet);		// zero all bits again
	cpu = CPU_ISSET(0, &cpuSet);
	setStr = cpu ? "set" : "not set";
	printf("\nafter clearing:  CPU 0 is %s in the mask\n", setStr);
	cpu = CPU_ISSET(1, &cpuSet);
	setStr = cpu ? "set" : "not set";
	printf("\nafter clearing:  CPU 1 is %s in the mask\n", setStr);
	retVal = sched_getaffinity(currPid, sizeof(cpu_set_t), &cpuSet);
	if ( retVal != 0 ) {
		printf("could not get processor affinity...\n");
		return -1;
	}
	printf("\nafter calling sched_setaffinity...\n\n");
	for(i = 0; i < 5; ++i) {
		cpu = CPU_ISSET(i, &cpuSet);
		setStr = cpu ? "set" : "not set";
		printf("CPU %d is %s in hard affinity\n", i, setStr);
	}

	printf("\nThis demonstrates that the affinity remains set after clearing "
			"the mask...\n\n");

    return 0;
}
