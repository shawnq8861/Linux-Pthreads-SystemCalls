/*****************************************************************************
 *
 * pthrdsThreeThrdsHWMapP9.c
 *
 * Modification to the example of creating three POSIX threads within a
 * single process, added writing to and reading from FPGA on-chip RAM
 * during hardware mapping.
 *
 * The program defined by this file encompasses step 9 of the progression
 * defined below.
 *
 * 	1.	Two threads that each print a message.
 * 	2.	Add turning an led on/off in each thread without interprocess
 * 		communication (IPC).
 * 	3.	Add mapping HPS and FPGA memory to virtual memory space, configure
 * 		one thread for HPS LEDs, the other thread for FPGA LEDs.
 * 	4.	Add either semaphore and mutex IPC.
 * 	5.	Add mTenna hardware mapping to a third process that also sets global
 * 		shared variables to indicate start and stop of the hardware mapping
 * 		process.  Use the global shared variable state to control turning
 * 		additional LEDs on/off.  Use standard linux timing functionality.
 * 	6.	Optimize the mTenna hardware mapping by:
 * 		a)	having other processes pend until execution is complete.
 * 		b)	setting the RT priority to maximum and locking memory to prevent
 * 			paging.
 * 		c)	isolating the mTenna hardware mapping to cpu1 with all other
 * 			processes running on cpu0.
 * 		d)	compare RT performance to results from step 5.
 * 	7.	Add higher resolution linux timer measurement, i.e. Posix clock,
 * 		and possibly GPIO pin toggling to allow scope measurement.
 * 	8.	Add reading GPIO and FPGA button states and starting hardware
 * 		mapping on button push.
 * 	9.	Add writing to and reading from FPGA on-chip Ram during mTenna
 * 		hardware mapping.
 *
 * Original code by Shawn Quinn
 * Created Date:  02/05/2015
 *
 ****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <stdint.h>
#include <sys/types.h>
#include "hardwareMapSoC.h"

// the following define the memory mapping for register access from the HPS
// GPIO1 addresses and bit settings
#define HPS_GPIO1_BASE 			0xFF709000	// base register address HPS GPIO1
#define	HPS_GPIO1_DDR_OFF_BYT	0x04		// byte offset to DDR reg address
#define HPS_GPIO1_LED0 			0x01000000	// word write value HPS LED0
#define HPS_GPIO1_LED1 			0x02000000	// word write value HPS LED1
#define HPS_GPIO1_LED2 			0x04000000	// word write value HPS LED2
#define HPS_GPIO1_LED3 			0x08000000	// word write value HPS LED3
#define HPS_GPIO1_ALL_ON		0x0F000000
#define	HPS_GPIO1_ALL_OFF		0x00000000

// GPIO2 addresses and bit settings
#define HPS_GPIO2_BASE 			0xFF70A000	// base register address HPS GPIO2
#define HPS_GPIO2_EXT_OFFSET	0x50
#define	HPS_GPIO2_DDR_OFF_BYT	0x04		// byte offset to DDR reg address
#define HPS_GPIO2_KEY0 			0x00200000	// word write value HPS button 0
#define HPS_GPIO2_KEY1 			0x00400000	// word write value HPS button 1
#define HPS_GPIO2_KEY2 			0x00800000	// word write value HPS button 2
#define HPS_GPIO2_KEY3 			0x01000000	// word write value HPS button 3
#define	HPS_GPIO2_ALL_OFF		0x00000000

// FPGA PIO addresses and bit settings
#define HPS_FPGA_SLAVE_BASE		0xFF200000	// base register address LW bridge
#define FPGA_PIO_LED_OFFSET		0x00010040	// byte offset to LED reg address
#define FPGA_PIO_LED0 			0x01		// byte write value to FPGA LED0
#define FPGA_PIO_LED1 			0x02		// byte write value to FPGA LED1
#define FPGA_PIO_LED2 			0x04		// byte write value to FPGA LED2
#define FPGA_PIO_LED3 			0x08		// byte write value to FPGA LED3
#define FPGA_PIO_LED_ALL_ON		0x0F
#define FPGA_PIO_LED_ALL_OFF	0x00
#define FPGA_PIO_KEY_OFFSET		0x000100C0	// byte offset to button reg addr
#define FPGA_PIO_KEY0 			0x01		// byte write value to FPGA button 0
#define FPGA_PIO_KEY1 			0x02		// byte write value to FPGA button 1
#define FPGA_PIO_KEY2 			0x04		// byte write value to FPGA button 2
#define FPGA_PIO_KEY3 			0x08		// byte write value to FPGA button 3
#define HPS_FPGA_MEM_BASE		0xC0000000	// base register address FPGA RAM
#define HPS_FPGA_MEM_SIZE		0x40000000	// FPGA RAM size in bytes
#define FPGA_PIO_RAM_OFFSET		0x00		// byte offset to on-chip memory
#define FPGA_PIO_ARR_OFFSET		0x00F		// byte offset to storage array
#define FPGA_PIO_BUF_OFFSET		0x80F		// byte offset to storage buffer

// other defined parameters...
#define PAGE_SIZE				4096		// linux page size
#define	MEAS_ARRAY_SIZE			50
#define MY_RT_PRIORITY 			99 			// Highest possible priority

// declare uninitialized task variables to pass to the tasks
pthread_t taskOneVar;
pthread_t taskTwoVar;
pthread_t taskThreeVar;

// initialize global shared variables to hold thread ids
pthread_t gThd1IdHolder = 0;
pthread_t gThd2IdHolder = 0;

// declare variables to store time measurement parameters
struct timespec tsStart;
struct timespec tsEnd;
uint32_t times[MEAS_ARRAY_SIZE];
clockid_t clkID = CLOCK_REALTIME;

// initialize global shared variable to hold thread loop counter
uint32_t gThdLoopCnt = 0;

// initialize global shared variable to hold measurement count
uint32_t measurementCnt = 0;

// declare a shared variable mutex to be used to coordinate loop count access
pthread_mutex_t sharedVariableMutex;

// declare a semaphore to coordinate LED toggle between tasks
sem_t semLED;

// statically allocate a buffer for the modulation data
uint32_t modBuff[MAX_SIZE];

// declare variables for use in mapping hardware registers into process space
int fdGpio1;					// file descriptor place holder for HPS GPIO1
int fdGpio2;					// file descriptor place holder for HPS GPIO2
int fdFpgaPio;					// file descriptor place holder for FPGA slave
int fdFpgaMem;					// file descriptor place holder for FPGA
								// on-chip RAM
volatile uint32_t*	gpio1BaseAddrPtr;	// holds return value from mmap call
volatile uint32_t*	gpio2BaseAddrPtr;	// holds return value from mmap call
volatile uint8_t*	fpgaPioBaseAddrPtr;	// holds return value from mmap call
volatile uint8_t*	fpgaMemBaseAddrPtr;	// holds return value from mmap call
uint32_t* timesPtr;				// pointer to hold start of array in FPGA RAM

// This is the master or producer task that signals the slave or consumer task
// when it is allowed to execute
void taskOne(void)
{
	printf("TaskOne process ID is %d\n", (int)getpid());
	gThd1IdHolder = pthread_self();
	printf("TaskOne thread ID is %d\n", (int)gThd1IdHolder);
	while(1) {
		if (gThdLoopCnt % 2) {
			// set the correct bit to turn on GPIO1 led one
			printf("turning GPIO1 led1 on...\n");
			*(gpio1BaseAddrPtr) = *(gpio1BaseAddrPtr) | HPS_GPIO1_LED1;
		}
		else {
			// turn off GPIO1 led one, read-modify-write
			printf("turning GPIO1 led1 off...\n");
			*(gpio1BaseAddrPtr) = *(gpio1BaseAddrPtr) & (~(HPS_GPIO1_LED1));
		}
		usleep(500000);


		// read one of the four GPIO buttons, assign the key number to the
		// buttonSelect variable to select the applicable MACRO definition
		uint32_t gpioButton;
		uint32_t gpioButtonSelect = 3;
		switch(gpioButtonSelect) {
		case 0:
			gpioButton = HPS_GPIO2_KEY0;
			break;
		case 1:
			gpioButton = HPS_GPIO2_KEY1;
			break;
		case 2:
			gpioButton = HPS_GPIO2_KEY2;
			break;
		case 3:
			gpioButton = HPS_GPIO2_KEY3;
			break;
		default:
			break;
		}
		if ( (( * ( (uint32_t*) ((uint8_t*)gpio2BaseAddrPtr + HPS_GPIO2_EXT_OFFSET) ) )
				&  gpioButton )  == 0) {
			printf("\nGPIO2 button key%u pressed...\n\n", gpioButtonSelect);
		}

		// Wait for the mutex before accessing the count variable
		pthread_mutex_lock(&sharedVariableMutex);
		gThdLoopCnt++;
		// cast the byte pointer to a word pointer...
		*((uint32_t*)(fpgaMemBaseAddrPtr + FPGA_PIO_RAM_OFFSET)) = 0xEEFF;
		printf("task one count = %d\n", gThdLoopCnt);

		// Release the mutex for the other task to use
		pthread_mutex_unlock(&sharedVariableMutex);

		if (!(gThdLoopCnt % 5)) {
			// post semaphore to signal task two to execute
			sem_post(&semLED);
		}
	}
	printf("\nTaskOne exiting...\n\n");
}

// This is the slave or consumer task under control of the master or
// producer task
void taskTwo(void)
{
	printf("TaskTwo process ID is %d\n", (int)getpid());
	gThd2IdHolder = pthread_self();
	printf("TaskTwo thread ID is %d\n", (int)gThd2IdHolder);
	while(1) {
		// pend on the semaphore from task one...
		sem_wait(&semLED);
		// set the correct bit to turn on FPGA led two
		printf("turning FPGA led2 on...\n");
		*(fpgaPioBaseAddrPtr + FPGA_PIO_LED_OFFSET) =
				*(fpgaPioBaseAddrPtr + FPGA_PIO_LED_OFFSET) | FPGA_PIO_LED2;
		usleep(1000000);
		// turn off FPGA led two, read-modify-write
		printf("turning FPGA led2 off...\n");
		*(fpgaPioBaseAddrPtr + FPGA_PIO_LED_OFFSET) =
			*(fpgaPioBaseAddrPtr + FPGA_PIO_LED_OFFSET) & (~(FPGA_PIO_LED2));

		// Wait for the mutex before accessing the count variable
		pthread_mutex_lock(&sharedVariableMutex);
		// modify the global shared variable..
		gThdLoopCnt++;
		// cast the byte pointer to a word pointer...
		printf("task two count = %d RAM value = %d\n", gThdLoopCnt,
				*((uint32_t*)(fpgaMemBaseAddrPtr + FPGA_PIO_RAM_OFFSET)));

		// Release the mutex for other task to use
		pthread_mutex_unlock(&sharedVariableMutex);

		// read one of the four FPGA buttons, assign the key number to the
		// buttonSelect variable to select the applicable MACRO definition
		uint32_t fpgaButton;
		uint32_t fpgaButtonSelect = 1;
		switch(fpgaButtonSelect) {
		case 0:
			fpgaButton = FPGA_PIO_KEY0;
			break;
		case 1:
			fpgaButton = FPGA_PIO_KEY1;
			break;
		case 2:
			fpgaButton = FPGA_PIO_KEY2;
			break;
		case 3:
			fpgaButton = FPGA_PIO_KEY3;
			break;
		default:
			break;
		}
		if ( (*(fpgaPioBaseAddrPtr + FPGA_PIO_KEY_OFFSET)  &  fpgaButton)   == 0) {
			printf("\nFPGA button key%u pressed...\n\n", fpgaButtonSelect);
		}
	}
}

// This is the master or producer task that executes the hardware mapping
// function.
void taskThree(void)
{
	int cpu;
	int retVal;
	uint32_t* timesPtr = (uint32_t*)(fpgaMemBaseAddrPtr + FPGA_PIO_ARR_OFFSET);
	//uint32_t* bufferPtr = (uint32_t*)(fpgaMemBaseAddrPtr + FPGA_PIO_BUF_OFFSET);
	pthread_t threadID;
	cpu_set_t cpuSet;
	printf("TaskThree process ID is %d\n", (int)getpid());
	threadID = pthread_self();
	printf("TaskThree thread ID is %d\n", (int)threadID);

	printf("\nzeroing the CPU mask...\n\n");
	CPU_ZERO(&cpuSet);		// zero out all bits in mask
	printf("\nsetting processor 1 with CPU_SET...\n\n");
	CPU_SET(1, &cpuSet);	// set bit for processor 1
	printf("\ncalling pthread_setaffinity_np()...\n\n");
	retVal = pthread_setaffinity_np(threadID, sizeof(cpu_set_t), &cpuSet);
	if ( retVal != 0 ) {
		printf("could not set processor affinity...\n");
	}
	CPU_ZERO(&cpuSet);		// zero all bits again
	cpu = CPU_ISSET(0, &cpuSet);
	char* setStr = cpu ? "set" : "not set";
	printf("\nafter clearing:  CPU 0 is %s in the mask\n", setStr);
	cpu = CPU_ISSET(1, &cpuSet);
	setStr = cpu ? "set" : "not set";
	printf("\nafter clearing:  CPU 1 is %s in the mask\n", setStr);
	retVal = pthread_getaffinity_np(threadID, sizeof(cpu_set_t), &cpuSet);
	if ( retVal != 0 ) {
		printf("could not get processor affinity...\n");
	}
	printf("\nafter calling pthread_getaffinity_np...\n\n");
	int i;
	for(i = 0; i < 2; ++i) {
		cpu = CPU_ISSET(i, &cpuSet);
		setStr = cpu ? "set" : "not set";
		printf("CPU %d is %s in hard affinity\n", i, setStr);
	}

	int rc;
	struct sched_param my_params;
	// Passing zero specifies caller’s (our) policy
	printf("\ncalling sched_setscheduler()...\n\n");
	my_params.sched_priority = MY_RT_PRIORITY;
	// Passing zero specifies callers (our) pid
	rc = sched_setscheduler(0, SCHED_FIFO, &my_params);
	if ( rc == -1 )
		printf("could not change scheduler policy\n");
	printf("\nlocking memory...\n\n");
	mlockall(MCL_CURRENT | MCL_FUTURE);

	sleep(1);
	while(gThdLoopCnt < 30) {
		// set the correct bit to turn on GPIO1 led one
		printf("turning GPIO1 led3 on...\n");
		*(gpio1BaseAddrPtr) = *(gpio1BaseAddrPtr) | HPS_GPIO1_LED3;

		// get the time at the start of the calculation
		retVal = clock_gettime (clkID, &tsStart);
		if (retVal < 0) {
			printf("\nerror reading clock\n\n");
		}

		// map the data
		calcModAndMapBits(modBuff);
		//calcModAndMapBits(bufferPtr);

		// get the time at the end of the calculation
		retVal = clock_gettime (clkID, &tsEnd);
		if (retVal < 0) {
			printf("\nerror reading clock\n\n");
		}
		// write each time measurement value into FPGA memory
		if (tsEnd.tv_nsec > tsStart.tv_nsec) {
			timesPtr[measurementCnt] = (tsEnd.tv_nsec - tsStart.tv_nsec);
			++measurementCnt;
		}

		usleep(100000);

		// turn off GPIO1 led three, read-modify-write
		printf("turning GPIO1 led3 off...\n");
		*(gpio1BaseAddrPtr) = *(gpio1BaseAddrPtr) & (~(HPS_GPIO1_LED3));

		usleep(100000);

	}
	printf("\nTaskThree exiting...\n\n");
	pthread_cancel(gThd1IdHolder);
	pthread_cancel(gThd2IdHolder);
}

int main(void)
{
	printf("The main process ID is %d\n", (int)getpid());

	// call open to obtain a file descriptor into virtual memory space
	printf("Attempting to open device file HPS GPIO1...\n\n");
	fdGpio1 = open( "/dev/mem", ( O_RDWR | O_SYNC ));
	if ( fdGpio1 == -1 ) {
		printf("Cannot open device file.\n");
	}

	// call open to obtain a file descriptor into virtual memory space
	printf("Attempting to open device file FPGA PIO...\n\n");
	fdFpgaPio = open( "/dev/mem", ( O_RDWR | O_SYNC ));
	if ( fdFpgaPio == -1 ) {
		printf("Cannot open device file.\n");
	}

	// call open to obtain a file descriptor into virtual memory space
	printf("Attempting to open device file HPS GPIO2...\n\n");
	fdGpio2 = open( "/dev/mem", ( O_RDWR | O_SYNC ));
	if ( fdGpio2 == -1 ) {
		printf("Cannot open device file.\n");
	}

	// call open to obtain a file descriptor into virtual memory space
	printf("Attempting to open device file FPGA MEM...\n\n");
	fdFpgaMem = open( "/dev/mem", ( O_RDWR | O_SYNC ));
	if ( fdFpgaMem == -1 ) {
		printf("Cannot open device file.\n");
	}

	// map one page of hardware addresses into virtual memory beginning at
	// the GPIO1 base address
	printf("Attempting to map GPIO1 Base Register address...\n\n");
	gpio1BaseAddrPtr = (volatile uint32_t*)mmap(NULL, PAGE_SIZE,
			PROT_READ | PROT_WRITE, MAP_SHARED, fdGpio1, HPS_GPIO1_BASE);

	if( gpio1BaseAddrPtr == MAP_FAILED ) {
		printf( "ERROR: mmap() GPIO1 failed...\n" );
		close( fdGpio1 );
	}

	// map 20 pages of hardware addresses into virtual memory beginning at
	// the FPGA slave base address, to allow accessing all FPGA peripherals
	printf("Attempting to map FPGA Slave Base Register address...\n\n");
	fpgaPioBaseAddrPtr = (volatile uint8_t*)mmap(NULL, 20 * PAGE_SIZE,
		PROT_READ | PROT_WRITE, MAP_SHARED, fdFpgaPio, HPS_FPGA_SLAVE_BASE);

	if( fpgaPioBaseAddrPtr == MAP_FAILED ) {
		printf( "ERROR: mmap() FPGA failed...\n" );
		close( fdFpgaPio );
	}

	// map one page of hardware addresses into virtual memory beginning at
	// the GPIO2 base address
	printf("Attempting to map GPIO2 Base Register address...\n\n");
	gpio2BaseAddrPtr = (volatile uint32_t*)mmap(NULL, PAGE_SIZE,
			PROT_READ | PROT_WRITE, MAP_SHARED, fdGpio2, HPS_GPIO2_BASE);

	if( gpio2BaseAddrPtr == MAP_FAILED ) {
		printf( "ERROR: mmap() GPIO2 failed...\n" );
		close( fdGpio2 );
	}

	// map 16 pages of hardware addresses into virtual memory beginning at
	// the FPGA memory base address, to allow accessing FPGA on-chip Ram
	printf("Attempting to map FPGA memory Base Register address...\n\n");
	fpgaMemBaseAddrPtr = (volatile uint8_t*)mmap(NULL, HPS_FPGA_MEM_SIZE,
		PROT_READ | PROT_WRITE, MAP_SHARED, fdFpgaMem, HPS_FPGA_MEM_BASE);

	if( fpgaMemBaseAddrPtr == MAP_FAILED ) {
		printf( "ERROR: mmap() FPGA failed...\n" );
		close( fdFpgaMem );
	}


	// set the direction bits for the GPIO1 LEDS by writing to the DDR reg
	*((uint32_t*)((uint8_t*)gpio1BaseAddrPtr + HPS_GPIO1_DDR_OFF_BYT))
			= HPS_GPIO1_ALL_ON;

	// set the direction bits for the GPIO2 buttons by writing to the DDR reg
	*((uint32_t*)((uint8_t*)gpio2BaseAddrPtr + HPS_GPIO2_DDR_OFF_BYT))
				= HPS_GPIO2_ALL_OFF;

	// write 0s to correct bits in the dr register to turn the leds off
	*(gpio1BaseAddrPtr) = *(gpio1BaseAddrPtr) & ~(HPS_GPIO1_ALL_ON);

	// Create the mutex for coordinating loop count shared variable access
	// by LED tasks
	pthread_mutex_init(&sharedVariableMutex, NULL);

	// Create the semaphore for LED tasks with an initial value of zero
	sem_init(&semLED, 0, 0);

	// create the three threads of execution
	pthread_create(&taskOneVar, NULL, (void*)taskOne, NULL);
	pthread_create(&taskTwoVar, NULL, (void*)taskTwo, NULL);
	pthread_create(&taskThreeVar, NULL, (void*)taskThree, NULL);

	// start the three threads
	pthread_join(taskOneVar, NULL);
	usleep(250000);
	pthread_join(taskTwoVar, NULL);
	usleep(250000);
	pthread_join(taskThreeVar, NULL);

	// write 0s to correct bits in the dr register to turn the leds off
	printf("\nturning all GPIO1 leds off...\n\n");
	*(gpio1BaseAddrPtr) = *(gpio1BaseAddrPtr) & ~(HPS_GPIO1_ALL_ON);

	// write 0s to the fpga pio register to turn the leds off
	printf("turning all FPGA leds off...\n\n");
	*(fpgaPioBaseAddrPtr + FPGA_PIO_LED_OFFSET) =
	   *(fpgaPioBaseAddrPtr + FPGA_PIO_LED_OFFSET) & (~(FPGA_PIO_LED_ALL_ON));

	// read out the time measurement values written to FPGA memory
	timesPtr = (uint32_t*)(fpgaMemBaseAddrPtr + FPGA_PIO_ARR_OFFSET);
	printf("\ntimer measurements (nsec):\n\n");
	int i;
	for (i = 0; i < measurementCnt; ++i) {
		printf("interval %d:  %u\n", i, timesPtr[i]);
	}

	printf("\nAttempting to unmap GPIO1 Base Register address...\n\n");
	if( munmap( (void*)gpio1BaseAddrPtr, PAGE_SIZE ) != 0 ) {
		printf( "ERROR: munmap() failed...\n" );
		close( fdGpio1 );
		return( 1 );
	}

	printf("Attempting to unmap FPGA Slave Base Register address...\n\n");
	if( munmap( (void*)fpgaPioBaseAddrPtr, 20 * PAGE_SIZE ) != 0 ) {
		printf( "ERROR: munmap() failed...\n" );
		close( fdFpgaPio );
		return( 1 );
	}

	printf("Attempting to unmap GPIO2 Base Register address...\n\n");
	if( munmap( (void*)gpio2BaseAddrPtr, PAGE_SIZE ) != 0 ) {
		printf( "ERROR: munmap() failed...\n" );
		close( fdGpio2 );
		return( 1 );
	}

	printf("Attempting to unmap FPGA Memory Base Register address...\n\n");
	if( munmap( (void*)fpgaMemBaseAddrPtr, 16 * PAGE_SIZE ) != 0 ) {
		printf( "ERROR: munmap() failed...\n" );
		close( fdFpgaMem );
		return( 1 );
	}

	printf("main exiting...\n\n");

	return 0;
}
