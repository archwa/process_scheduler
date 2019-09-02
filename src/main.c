#include <stdio.h>
#include "sched.h"

// preprocessor variables for easier testing
#define CHILD_COUNT         5
#define TICK_MAX          100

#define NICE_MULTIPLIER     5
#define NICE_OFFSET        20

#define GET_NICE(X)  X * NICE_MULTIPLIER - NICE_OFFSET

// the testbed which contains the simulated process environment
void testbed () {
	int i, rc, cpid;

	// assign nicest value to init
	sched_nice (19);

	// spawn children
	for (i = 0; i < CHILD_COUNT; ++i) {
		switch (cpid = sched_fork ()) {
			case -1:
				break;
			case 0:  // in child
				// assign a new nice value based on i
				sched_nice (GET_NICE(i));

				// spin around for a little bit
				while (sched_gettick () < TICK_MAX);
				
				// time to curl up in a ball and die; return our total cpu time
				sched_exit (sched_gettick ());
			default: // in parent
				printf ("\n>>> Parent process %d spawned child process %d!\n", sched_getpid (), cpid);
		}
	}

	// wait for each child to die
	for (i = 0; i < CHILD_COUNT; ++i) {
		cpid = sched_wait (&rc);
		printf ("\n>>> Child process %d exited and returned %d!\n", cpid, rc);
	}

	sched_exit (0);
}

int main (int argc, char ** argv) {
	printf ("\nInit process returned with %d! Exiting program...\n", sched_init (testbed));
	
	return 0;
}
