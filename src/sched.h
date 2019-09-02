#ifndef __SCHED_H__
#define __SCHED_H__

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include "savectx64.h"

#define SCHED_NPROC    4096              // 1 <= pid <= SCHED_NPROC
#define SCHED_READY       0
#define SCHED_RUNNING     1
#define SCHED_SLEEPING    2
#define SCHED_ZOMBIE      3
#define SCHED_SWITCH_RET  4
#define SCHED_EXIT_RET    5
#define SCHED_INIT_RET    6

#define STACK_SIZE    65536              // in bytes (length of mapping for stack)

struct savectx global_ctx;               // the global context of the container of init

extern int adjstack ();                  // fix the saved %rbp regs in a given stack

// node of doubly-linked list expressing a set of processes
struct sched_procnode {
	struct sched_procnode * prev;        // previous sched_procnode
	struct sched_procnode * next;        // next sched_procnode
	struct sched_proc * proc;            // pointer to associated process struct sched_proc
};

// process information structure
struct sched_proc {
	unsigned short int task_state;       // READY, RUNNING, SLEEPING, ZOMBIE
	unsigned long long cpu_time;         // time the process has been on the cpu in total (in ticks)
	unsigned long long slice_max;        // how long the process has to do its thing (in ticks)
	unsigned long long slice_acc;        // how long the process has been on the cpu since last scheduled (in ticks)
	unsigned short int priority;         // 0 to 39 (used by scheduler)
	signed short int nice;               // -20 to 19 (used by scheduler)
	unsigned int pid;                    // process ID
	unsigned int ppid;                   // parent process ID
	int exit_code;                       // the exit code of the process
	void * stack_base;                   // pointer to the BASE of the stack (TOP of stack is in LOWER memory)
	struct savectx pctx;                 // contains context regs, including base ptr, stack ptr, and prog counter
	struct sched_proc * parent;          // pointer to the parent sched_proc
	struct sched_procnode * my_procnode; // pointer to the procnode for this sched_proc
	struct sched_procnode child_anchor;  // doubly-linked list of children's sched_proc
};

// current holds a pointer to the current process
struct sched_proc * current;

// doubly-linked list of all living processes (including zombies)
struct sched_procnode proc_anchor;

// holds information about which pids are available for claiming
unsigned short int pid_table[SCHED_NPROC + 1];

// these work like setjmp and longjmp ((re)storing the context (registers))
int savectx (struct savectx * ctx);
void restorectx (struct savectx * ctx, int retval);

// sched_init (void (* init_fn) ());
//   This function will be called once by the testbed program.
//   It should initialize your scheduling system, including
//   setting up a periodic interval timer (see setitimer),
//   establishing sched_tick as the signal handler for
//   that timer, and creating the initial task which will
//   have pid of 1.  After doing so, make pid 1 runnable and
//   transfer execution to it (including switching to its
//   stack) at location init_fn.  This init_fn function is not
//   expected to return and if it does so it is OK to
//   have unpredictable results.
signed short int sched_init (void (* init_fn) ());

// sched_fork ();
//   Just like the real fork, create a new simulated task which
//   is a copy of the caller.  Allocate a new pid for the
//   child, and make the child runnable and eligible to
//   be scheduled.  sched_fork returns 0 to the child and
//   the child's pid to the parent.  It is not required that
//   the relative order of parent vs child being scheduled
//   be defined.  On error, return -1.
int sched_fork ();

// sched_exit (int code);
//   Terminate the current task, making it a ZOMBIE, and store
//   the exit code.  If a parent is sleeping in sched_wait (),
//   wake it up and return the exit code to it.
//   There will be no equivalent of SIGCHLD.  sched_exit
//   will not return.  Another runnable process will be scheduled.
void sched_exit (int code);

// sched_wait (int * exit_code);
//   Return the exit code of a zombie child and free the
//   resources of that child.  If there is more
//   than one such child, the order in which the codes are
//   returned is not defined.  If there are no zombie children,
//   but the caller does have at least one child, place
//   the caller in SLEEPING, to be woekn up when a child
//   calls sched_exit ().  If there are no children, return
//   immediately with -1, otherwise the return value is the
//   pid of the child whose status is being returned.
//   Since there are no simulated signals, the exit code
//   is simply the integer from sched_exit ().
int sched_wait (int * exit_code);

// sched_nice (int niceval);
//   Set the current taks's "nice value" to the supplied parameter.
//   Nice values may range from +19 (least preferred static
//   priority) to -20 (most preferred).  Clamp any out-of-range
//   values to those limits.
void sched_nice (signed short int niceval);

// sched_getpid ();
//   Return current task's pid.
unsigned int sched_getpid ();

// sched_getppid ();
//   Return pid of the parent of current task.
unsigned int sched_getppid ();

// sched_gettick ();
//   Return the number of timer ticks since startup.
unsigned long long sched_gettick ();

// sched_ps ();
//   Output to stderr a listing of all of the current tasks,
//   including sleeping and zombie tasks.  List the
//   following information in tabular form:
//       pid
//       ppid
//       current state
//       base addr of private stack area
//       static priority
//       dynamic priority info (see below)
//       total CPU time used (in ticks)
//   ** "dynamic priority" will vary in interpretation and range
//   depending on what scheduling algorithm you use.  E.g.
//   if you follow the CFS outline, then vruntime will be the
//   best indicator of dynamic priority.
//
//   You should establish sched_ps () as the signal handler
//   for SIGABRT so that a ps can be forced at any
//   time by sending the testbed SIGABRT.
void sched_ps ();

// sched_switch ();
//   This is the suggested name of a required routine which will
//   never be called directly by the testbed.  sched_switch ()
//   should be the sole place where a context switch is made,
//   analogous to schedule () within the Linux kernel.
//   sched_switch () should place the current task on the run queue
//   (assuming it is READY), then select the best READY task from
//   the runqueue, taking into account the dynamic priority
//   of each task.  The selected task should then be placed
//   in the RUNNING state and a context switch made to it
//   (unless, of course, the best task is also the current task).
//   See discussion below on support routines for context switch.
int sched_switch ();

// sched_tick ();
//   This is the suggested name of a required routine which will
//   never be called directly by the testbed, but instead will
//   be the signal handler for the SIGVTALRM signal generated by
//   the periodic timer.  Each occurrence of the timer signal
//   is considered a tick.  The number of ticks since sched_init
//   is to be returned by sched_gettick ().
//   sched_tick should examine the currently running
//   task and if its time slice has just expired, mark that
//   task as READY, place it on the run queue based on its
//   current dynamic priority, and then call sched_switch ()
//   to cause a new task to be run.  Watch out for
//   mask issues...remember SIGVTALARM will, by default,
//   be masked on entry to your signal handler.
void sched_tick ();

// sched_blocksigs (sigset_t * block_sigset, sigset_t * old_sigset);
//   Attempts to block all signals given by block_sigset, and
//   stores the old signal mask in old_sigset for later restoration.
int sched_blocksigs (sigset_t * block_sigset, sigset_t * old_sigset);

// sched_unblocksigs (sigset_t * block_sigset, sigset_t * old_sigset);
//   Attempts to unblock all signals given by block_sigset, and
//   restores the old signal mask given by old_sigset.
int sched_unblocksigs (sigset_t * block_sigset, sigset_t * old_sigset);

// sched_getunusedpid ();
//   Returns an unused pid in the pid_table.
//   Returns 0 if no pids remain.
unsigned short int sched_getunusedpid ();

#endif
