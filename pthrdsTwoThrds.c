/*****************************************************************************
 *
 * pthrdsTwoThrds.c
 *
 * Example of creating two POSIX threads within a single process
 *
 * The program defined by this file is step 1 of the progression defined
 * below.
 *
 * 	1.	Two threads that each print a message
 * 	2.	Add turning an led on/off to each thread
 * 	3.	Add either semaphore and/or mutex IPC
 * 	4.	Add mTx hardware mapping to a third process that sets global shared
 * 		variables to indicate start and stop, which signal to turn additional
 * 		leds on/off
 * 	5.	Add high resolution linux timer measurement
 * 	6.	Add starting hardware mapping on button push
 * 	7.	Add mapping HPS and FPGA memory cells to virtual memory space for
 * 		the process and demonstrate read/write
 *
 * Original code by Shawn Quinn
 * Created Date:  01/16/2015
 *
 ****************************************************************************/

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

// declare uninitialized task variables to pass to the tasks
pthread_t taskOneVar;
pthread_t taskTwoVar;

void taskOne(void)
{
	printf("TaskOne process ID is %d\n", (int)getpid());
	printf("TaskOne thread ID is %d\n", (int)pthread_self());
	int count = 0;
	while(count < 30) {
		usleep(500000);
		printf("task one count = %d\n", count);
		++count;
	}
}

void taskTwo(void)
{
	printf("TaskTwo process ID is %d\n", (int)getpid());
	printf("TaskTwo thread ID is %d\n", (int)pthread_self());
	int count = 0;
	while(count < 30) {
		usleep(250000);
		printf("task two count = %d\n", count);
		++count;
	}
}

int main(void)
{
	printf("The main process ID is %d\n", (int)getpid());

	// create the two threads of execution
	pthread_create(&taskOneVar, NULL, (void*)taskOne, NULL);
	pthread_create(&taskTwoVar, NULL, (void*)taskTwo, NULL);

	// start the two threads
	pthread_join(taskOneVar, NULL);
	usleep(250000);
	pthread_join(taskTwoVar, NULL);

	printf("\nmain exiting...\n");

	return 0;
}
