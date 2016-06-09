/*****************************************************************************
 *
 * pthrdsTwoThrdsIPC.c
 *
 * Example of creating two POSIX threads within a single process
 *
 * The program defined by this file encompasses step 4 of the
 * progression defined below.
 *
 * 	1.	Two threads that each print a message
 * 	2.	Add turning an led on/off in each thread without interprocess
 * 		communication (IPC)
 * 	3.	Add mapping HPS and FPGA memory to virtual memory space, configure
 * 		one thread for HPS LEDs, the other thread for FPGA LEDs
 * 	4.	Add either semaphore and mutex IPC
 * 	5.	Add mTenna hardware mapping to a third process that also sets global
 * 		shared variables to indicate start and stop of the hardware mapping
 * 		process.  Use the global shared variable state to control turning
 * 		additional LEDs on/off.  Use built-n linux timing functionality
 * 	6.	Optimize the mTenna hardware mapping by:
 * 		a)	having other processes pend until execution is complete
 * 		b)	setting the RT priority to maximum and locking memory to prevent
 * 			paging
 * 		c)	isolating the mTenna hardware mapping to cpu1 with all other
 * 			processes running on cpu0.
 * 		d)	compare RT performance to results from step 5
 * 	7.	Add high resolution linux timer measurement and possibly GPIO pin
 * 		toggling to allow scope measurement.
 * 	8.	Add starting hardware mapping on button push
 * 	9.	Add writing to and reading from FPGA on-chip Ram during mTenna
 * 		hardware mapping
 *
 * Original code by Shawn Quinn
 * Created Date:  01/26/2015
 *
 ****************************************************************************/

#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>

// the following define the memory mapping for register access from the HPS

#define HPS_GPIO1_BASE 			0xFF709000	// base register address HPS GPIO1
#define	HPS_GPIO1_DDR_OFF_BYT	0x04		// byte offset to DDR reg address
#define HPS_GPIO1_LED0 			0x01000000	// word write value HPS LED0
#define HPS_GPIO1_LED1 			0x02000000	// word write value HPS LED1
#define HPS_GPIO1_LED2 			0x04000000	// word write value HPS LED2
#define HPS_GPIO1_LED3 			0x08000000	// word write value HPS LED3
#define HPS_GPIO1_ALL_ON		0x0F000000
#define	HPS_GPIO1_ALL_OFF		0x00000000
#define HPS_FPGA_SLAVE_BASE		0xFF200000	// base register address LW bridge
#define FPGA_PIO_LED_OFFSET		0x00010040	// byte offset to LED reg address
#define FPGA_PIO_LED0 			0x01		// byte write value to FPGA LED0
#define FPGA_PIO_LED1 			0x02		// byte write value to FPGA LED1
#define FPGA_PIO_LED2 			0x04		// byte write value to FPGA LED2
#define FPGA_PIO_LED3 			0x08		// byte write value to FPGA LED3
#define FPGA_PIO_LED_ALL_ON		0x0F
#define FPGA_PIO_LED_ALL_OFF	0x00
#define PAGE_SIZE				4096		// linux page size

// declare uninitialized task variables to pass to the tasks
pthread_t taskOneVar;
pthread_t taskTwoVar;

// initialize global shared variable to hold thread id
pthread_t gThdIdHolder = 0;

// initialize global shared variable to hold thread loop counter
uint32_t gThdLoopCnt = 0;

// declare a shared variable mutex to be used to coordinate loop count access
pthread_mutex_t sharedVariableMutex;

// declare a semaphore to coordinate LED toggle between tasks
sem_t semLED;

// declare variables for use in mapping hardware registers into process space
int fdGpio;					// file descriptor place holder for HPS GPIO
int fdFpgaPio;				// file descriptor place holder for FPGA slave
volatile uint32_t*	gpio1BaseAddrPtr;	// holds return value from mmap call
volatile uint8_t*	fpgaPioBaseAddrPtr;	// holds return value from mmap call

// this is the master or producer task that signals the slave or consumer task
// when it is allowed to execute
void taskOne(void)
{
	printf("TaskOne process ID is %d\n", (int)getpid());
	printf("TaskOne thread ID is %d\n", (int)pthread_self());
	while(gThdLoopCnt < 30) {
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

		// Wait for the mutex before accessing the count variable
		pthread_mutex_lock(&sharedVariableMutex);

		gThdLoopCnt++;

		printf("task one count = %d\n", gThdLoopCnt);

		// Release the mutex for the other task to use
		pthread_mutex_unlock(&sharedVariableMutex);

		if (!(gThdLoopCnt % 5)) {
			// post semaphore to signal task two to execute
			sem_post(&semLED);
		}
	}
	printf("\nTaskOne exiting...\n\n");
	pthread_cancel(gThdIdHolder);
}

// This is the slave or consumer task under control of the master or
// producer task
void taskTwo(void)
{
	printf("TaskTwo process ID is %d\n", (int)getpid());
	gThdIdHolder = pthread_self();
	printf("TaskTwo thread ID is %d\n", (int)gThdIdHolder);
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

		gThdLoopCnt++;

		printf("task two count = %d\n", gThdLoopCnt);

		// Release the mutex for other task to use
		pthread_mutex_unlock(&sharedVariableMutex);
	}
}

int main(void)
{
	printf("The main process ID is %d\n", (int)getpid());

	// call open to obtain a file descriptor into virtual memory space
	printf("Attempting to open device file HPS GPIO...\n\n");
	fdGpio = open( "/dev/mem", ( O_RDWR | O_SYNC ));
	if ( fdGpio == -1 ) {
		printf("Cannot open device file.\n");
	}

	// call open to obtain a file descriptor into virtual memory space
	printf("Attempting to open device file FPGA PIO...\n\n");
	fdFpgaPio = open( "/dev/mem", ( O_RDWR | O_SYNC ));
	if ( fdFpgaPio == -1 ) {
		printf("Cannot open device file.\n");
	}

	// map one page of hardware addresses into virtual memory beginning at
	// the GPIO1 base address
	printf("Attempting to map GPIO1 Base Register address...\n\n");
	gpio1BaseAddrPtr = (volatile uint32_t*)mmap(NULL, PAGE_SIZE,
			PROT_READ | PROT_WRITE, MAP_SHARED, fdGpio, HPS_GPIO1_BASE);

	if( gpio1BaseAddrPtr == MAP_FAILED ) {
		printf( "ERROR: mmap() GPIO failed...\n" );
		close( fdGpio );
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

	// set the direction bits for the GPIO LEDS by writing to the DDR register
	*((uint32_t*)((uint8_t*)gpio1BaseAddrPtr + HPS_GPIO1_DDR_OFF_BYT))
			= HPS_GPIO1_ALL_ON;

	// write 0s to correct bits in the dr register to turn the leds off
	*(gpio1BaseAddrPtr) = *(gpio1BaseAddrPtr) & ~(HPS_GPIO1_ALL_ON);

	// Create the mutex for coordinating loop count shared variable access
	// by LED tasks
	pthread_mutex_init(&sharedVariableMutex, NULL);

	// Create the semaphore for LED tasks with an initial value of zero
	sem_init(&semLED, 0, 0);

	// create the two threads of execution
	pthread_create(&taskOneVar, NULL, (void*)taskOne, NULL);
	pthread_create(&taskTwoVar, NULL, (void*)taskTwo, NULL);

	// start the two threads
	pthread_join(taskOneVar, NULL);
	usleep(250000);
	pthread_join(taskTwoVar, NULL);

	// write 0s to correct bits in the dr register to turn the leds off
	printf("\nturning all GPIO1 leds off...\n\n");
	*(gpio1BaseAddrPtr) = *(gpio1BaseAddrPtr) & ~(HPS_GPIO1_ALL_ON);

	// write 0s to the fpga pio register to turn the leds off
	printf("turning all FPGA leds off...\n\n");
	*(fpgaPioBaseAddrPtr + FPGA_PIO_LED_OFFSET) =
		*(fpgaPioBaseAddrPtr + FPGA_PIO_LED_OFFSET) & (~(FPGA_PIO_LED_ALL_ON));

	printf("Attempting to unmap GPIO1 Base Register address...\n\n");
	if( munmap( (void*)gpio1BaseAddrPtr, PAGE_SIZE ) != 0 ) {
			printf( "ERROR: munmap() failed...\n" );
			close( fdGpio );
			return( 1 );
		}

	printf("Attempting to unmap FPGA Slave Base Register address...\n\n");
		if( munmap( (void*)fpgaPioBaseAddrPtr, PAGE_SIZE ) != 0 ) {
				printf( "ERROR: munmap() failed...\n" );
				close( fdFpgaPio );
				return( 1 );
			}

	printf("main exiting...\n\n");

	return 0;
}
