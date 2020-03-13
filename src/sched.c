#include "sched.h"

signed short int sched_init (void (* init_fn) ()) {
	// initialize pid_table to 0
	memset (pid_table, 0, SCHED_NPROC + 1);

	// initialize the process anchor (doubly-linked list that holds all living processes)
	proc_anchor.prev = &proc_anchor;
	proc_anchor.next = &proc_anchor;
	proc_anchor.proc = NULL;

	// set up stack address space for init process
	void * new_sp;
	if ((new_sp = mmap (0, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0)) == MAP_FAILED) {
		fprintf (stderr, "ERROR: Init process could not be created!\n");
		fprintf (stderr, "--> mmap() failure: %s\n", strerror (errno));
		return -1;
	}

	// set up context for new process (set base pointer and stack pointer to stack address space + STACK_SIZE)
	//   this will place the stack pointer and base pointer at the bottom of the stack
	struct savectx init_ctx;
	savectx (&init_ctx);                        // spill the registers into the struct savectx "init_ctx"
	init_ctx.regs[JB_BP] = new_sp + STACK_SIZE; // set base pointer
	init_ctx.regs[JB_SP] = new_sp + STACK_SIZE; // set stack pointer
	init_ctx.regs[JB_PC] = init_fn;             // and program counter

	// set up new sched_proc for the init process
	struct sched_proc proc_init;
	proc_init.task_state = SCHED_RUNNING;       // this is going to be running here in a second
	proc_init.cpu_time = 0;                     // no ticks on cpu so far (new process)
	proc_init.slice_max = 21;                   // initialize time slice info
	proc_init.slice_acc = 0;
	proc_init.priority = 20;                    // default 20 as priority
	proc_init.nice = 0;                         // default 0 as nice
	proc_init.pid = 1;                          // set init process id to 1
	proc_init.ppid = 1;                         // it is its own parent
	proc_init.exit_code = 0;                    // exit_code is 0 for now
	proc_init.stack_base = new_sp + STACK_SIZE; // save pointer to bottom of stack in stack_base
	proc_init.pctx = init_ctx;                  // contains context regs
	proc_init.parent = &proc_init;              // contains pointer to itself
	proc_init.child_anchor.proc = NULL;         // anchor doesn't have associated process
	proc_init.child_anchor.prev = &proc_init.child_anchor; // pointer to self
	proc_init.child_anchor.next = &proc_init.child_anchor; // pointer to self
	
	// set up init process procnode for the "living" process doubly-linked list
	struct sched_procnode init_procnode;
	init_procnode.prev = &proc_anchor;          // pointer to living process list anchor
	init_procnode.next = &proc_anchor;          // pointer to living process list anchor
	proc_anchor.prev = &init_procnode;          // pointer to first process (init process)
	proc_anchor.next = &init_procnode;          // pointer to first process (init process)
	init_procnode.proc = &proc_init;            // pointer to the actual init process
	proc_init.my_procnode = &init_procnode;     // pointer to the init procnode

	// point current process pointer to the init process
	current = &proc_init;

	// record in pid_table that pid 1 is now in use
	pid_table[1] = 1;

	// establish sched_tick() as signal handler for that timer
	if (signal (SIGVTALRM, sched_tick) == SIG_ERR) {
		fprintf (stderr, "ERROR: Init process could not be created!\n");
		fprintf (stderr, "--> signal() failure: %s\n", strerror (errno));
		return -1;
	}

	// establish sched_ps() as signal handler for SIGABRT [abort() calls]
	if (signal (SIGABRT, sched_ps) == SIG_ERR) {
		fprintf (stderr, "ERROR: Init process could not be created!\n");
		fprintf (stderr, "--> signal() failure: %s\n", strerror (errno));
		return -1;
	}

	// set up itimer for every 100ms (one-hundred thousand microseconds)
	struct itimerval itv;
	itv.it_interval.tv_sec = 0;
	itv.it_interval.tv_usec = 100000;
	itv.it_value.tv_sec = 0;
	itv.it_value.tv_usec = 100000;

	// set up periodic interval timer (setitimer)
	if (setitimer (ITIMER_VIRTUAL, &itv, NULL) < 0) {
		fprintf (stderr, "ERROR: Init process could not be created!\n");
		fprintf (stderr, "--> setitimer() failure: %s\n", strerror (errno));
		return -1;
	}

	// save global context (this allows the init process to return to the container)
	if (savectx (&global_ctx) == SCHED_INIT_RET) {
		return 0;
	}

	// transfer execution to init_fn, which has its own user-level stack (init)
	restorectx (&proc_init.pctx, 0);
}

int sched_fork () {
	sigset_t block_sigset, old_sigset;

	// block all signals
	sigfillset (&block_sigset);
	if (sched_blocksigs (&block_sigset, &old_sigset) < 0) {
		fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
		fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		return -1;
	}

	// set up stack address space for child process
	void * new_sp;
	if ((new_sp = mmap (0, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0)) == MAP_FAILED) {
		fprintf (stderr, "ERROR: Child process could not be created!\n");
		fprintf (stderr, "--> mmap() failure: %s\n", strerror (errno));
		if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) { // unblock signals
			fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
			fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		}
		return -1;
	}

	// copy parent stack into child stack
	memcpy (new_sp, (current->stack_base - STACK_SIZE), STACK_SIZE);

	// calculate stack offset from parent stack to child stack
	unsigned long stack_offset = ((unsigned long) (new_sp + STACK_SIZE - current->stack_base));

	// set up context for new process (set base pointer and stack pointer to BOTTOM of stack address space)
	struct savectx child_ctx;
	if (savectx (&child_ctx) == SCHED_SWITCH_RET) {
		if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) { // unblock signals set in sched_switch
			fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
			fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		}
		return 0;
	}
	adjstack (new_sp, new_sp + STACK_SIZE, stack_offset);
	child_ctx.regs[JB_BP] += stack_offset; // offset the base pointer and stack pointer for the
	child_ctx.regs[JB_SP] += stack_offset; //   child's stack (given the parent's bp & sp)

	// allocate memory for the new child process sched_proc (*current is the parent)
	struct sched_proc * child_proc;
	if ((child_proc = (struct sched_proc *) malloc (sizeof (struct sched_proc))) == NULL) {
		// no memory left! cannot create child process
		fprintf (stderr, "ERROR: Child process could not be created!\n");
		fprintf (stderr, "--> malloc() failure: %s\n", strerror (errno));
		if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) { // unblock signals
			fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
			fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		}
		return -1;
	}

	// set up child_proc information
	child_proc->task_state = SCHED_READY; // let child process be schedulable
	child_proc->cpu_time = 0;             // it hasn't been on the cpu yet
	child_proc->slice_max = 21;
	child_proc->slice_acc = 0;
	child_proc->priority = 20;            // default is 20
	child_proc->nice = current->nice;
	if ((child_proc->pid = sched_getunusedpid ()) == 0) {
		// max proc limit reached! cannot create child process;
		//   release the allocated memory & clean things up
		fprintf (stderr, "ERORR: Child process could not be created!\n");
		fprintf (stderr, "--> Maximum process limit reached! (%d)\n", SCHED_NPROC);
		free (child_proc);
		munmap (new_sp, STACK_SIZE); // unmap the stack
		if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) { // unblock signals
			fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
			fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		}
		return -1;
	}
	child_proc->ppid = current->pid;                           // store current's pid as child's ppid
	child_proc->exit_code = 0;                                 // exit_code is 0 for now
	child_proc->stack_base = new_sp + STACK_SIZE;              // save pointer to TOP OF STACK (LOWER ADDRESS!)
	child_proc->pctx = child_ctx;                              // store child context to child
	child_proc->parent = current;                              // store pointer to current process (parent)
	child_proc->child_anchor.prev = &child_proc->child_anchor; // pointer to self
	child_proc->child_anchor.next = &child_proc->child_anchor; // pointer to self
	child_proc->child_anchor.proc = NULL;

	// allocate memory for child sched_procnodes (one for proc_anchor, one for parent's child_anchor)
	struct sched_procnode * child_procnode1, * child_procnode2;
	if ((child_procnode1 = (struct sched_procnode *) malloc (sizeof (struct sched_procnode))) == NULL
		|| (child_procnode2 = (struct sched_procnode *) malloc (sizeof (struct sched_procnode))) == NULL) {
		// no memory left! cannot create child process;
		//   release the allocated memory & clean things up
		fprintf (stderr, "ERORR: Child process could not be created!\n");
		fprintf (stderr, "--> malloc() failure: %s\n", strerror (errno));
		free (child_procnode1);
		free (child_procnode2);
		free (child_proc);
		munmap (new_sp, STACK_SIZE); // unmap the stack
		if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) { // unblock signals
			fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
			fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		}
		return -1;
	}

	// assign child's procnode pointer to the child's sched_proc "my_procnode"
	child_proc->my_procnode = child_procnode1;

	// update proc_anchor list of living processes (insert the child to the right of the parent procnode)
	child_procnode1->prev = current->my_procnode;       // set child's procnode's prev to the parent procnode
	child_procnode1->next = current->my_procnode->next; // set child's procnode's next to parent's right procnode
	child_procnode1->next->prev = child_procnode1;      // set right node's previous to child's procnode
	current->my_procnode->next = child_procnode1;       // set parent's next to child's procnode
	child_procnode1->proc = child_proc;                 // set proc pointer to the child's sched_proc (its own)

	// update the parent's list of children (insert the child at the front of the list [child_anchor])
	child_procnode2->prev = (&current->child_anchor);   // set child's procnode's prev to the child anchor
	child_procnode2->next = current->child_anchor.next; // set child's procnode's next to the child following it
	current->child_anchor.next->prev = child_procnode2; // set parent's 1 child in list's previous to child procnode
	current->child_anchor.next = child_procnode2;       // set set parent's 1 child in list to child procnode
	child_procnode2->proc = child_proc;                 // pointer to the child's sched_proc (its own)

	// record in pid_table that pid child_proc->pid is now in use
	pid_table[child_proc->pid] = 1;

	// unblock and restore signals
	if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) {
		fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
		fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		return -1;
	}

	// return child pid to parent
	return child_proc->pid;
}

void sched_exit (int code) {
	sigset_t block_sigset, old_sigset;

	// block all signals
	sigfillset (&block_sigset);
	if (sched_blocksigs (&block_sigset, &old_sigset) < 0) {
		fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
		fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
	}

	// if the current process is init, simply restore the global context
	if (current->pid == 1) {
		restorectx (&global_ctx, SCHED_INIT_RET);
	}

	// re-parent any children to current's parent (i.e., put them into the parent's children
	//   doubly-linked list AND update their parent pointers and ppid) ONLY IF there are children
	if (current->child_anchor.next != &current->child_anchor) {
		struct sched_procnode * pn;
		for (pn = current->child_anchor.next; pn->proc != NULL; pn = pn->next) {
			pn->proc->ppid = current->ppid;     // update children ppid to parent's pid
			pn->proc->parent = current->parent; // update children parent pointers to parent
		}

		// append child list to sibling list
		current->child_anchor.next->prev = &current->parent->child_anchor;
		current->child_anchor.prev->next = current->parent->child_anchor.next;
		current->parent->child_anchor.next = current->child_anchor.next; // put at front of parent's child list
		current->parent->child_anchor.next->prev = current->child_anchor.prev;
	}
	current->task_state = SCHED_ZOMBIE;         // process is now a ZOMBIE!!!
	current->exit_code = code;                  // set exit code
	pid_table[current->pid] = 0;                // free up pid, but keep pid info in our sched_proc

	// sched_switch checks to see if zombie's parent is SLEEPING
	//   if the parent is SLEEPING, the parent is rescheduled;
	//   otherwise, another process is scheduled
	sched_switch ();
}

int sched_wait (int * exit_code) {
	sigset_t block_sigset, old_sigset;

	// block all signals
	sigfillset (&block_sigset);
	if (sched_blocksigs (&block_sigset, &old_sigset) < 0) {
		fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
		fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
	}
	
	// return if there are no children to kill at all
	if (current->child_anchor.next == &current->child_anchor) {
		fprintf (stderr, "ERROR: Process %d has no zombie to kill!\n", current->pid);
		fprintf (stderr, "--> sched_wait() failure\n");
		if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) {
			fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
			fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		}
		return -1;
	}

	int z_flag;	
	z_flag = 0;

	// check to see if there are zombie children
	struct sched_procnode * pn;
	for (pn = current->child_anchor.next; pn->proc != NULL; pn = pn->next) {
		if (pn->proc->task_state == SCHED_ZOMBIE) {
			z_flag = 1;
			break;
		}
	}

	// if there are no zombies but there are children, we go to sleep and wait for zombies
	if (z_flag == 0) {
		current->task_state = SCHED_SLEEPING;    // switch to sleeping state
		current->slice_acc = 0;                  // reset the time slice accumulator
		if (savectx (&current->pctx) != SCHED_EXIT_RET) {
			sched_switch ();                     // relinquish to another process
		} else { // we have been awoken by a zombie child
			current->task_state = SCHED_RUNNING; // switch to running state

			// print information about all living processes (debug)
			sched_ps ();                         // debug info
		}
	}

	// kill the zombie children
	int rc, z_pid;
	struct sched_proc * sproc;
	for (pn = current->child_anchor.next; pn->proc != NULL; pn = pn->next) {
		if (pn->proc->task_state == SCHED_ZOMBIE) {
			rc = pn->proc->exit_code;
			z_pid = pn->proc->pid;

			// remove child proc node from proc anchor living process list
			pn->proc->my_procnode->next->prev = pn->proc->my_procnode->prev;
			pn->proc->my_procnode->prev->next = pn->proc->my_procnode->next;

			// remove child proc node from the child proc list
			pn->next->prev = pn->prev;
			pn->prev->next = pn->next;

			// free all dynamically allocated resources used by the zombie child
			sproc = pn->proc;                                      // store the sched_proc before burning procnode
			free (pn->proc->my_procnode);                          // incinerate the child sched_procnode (living proc list)
			free (pn);                                             // free the sched_procnode of the child
			munmap ((sproc->stack_base - STACK_SIZE), STACK_SIZE); // unmap the stack of the child
			free (sproc);                                          // free the sched_proc of the child
		}
	}

	// store last zombie exit code in *exit_code (only if it is a valid pointer)
	if (exit_code != NULL) {
		*exit_code = rc; 
	}
	
	// unblock and restore signals
	if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) {
		fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
		fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
	}

	// return the last zombie pid
	return z_pid;
}


void sched_nice (signed short int niceval) {
	if (niceval >= -20 && niceval <= 19) {
		current->nice = niceval;
	}
}

unsigned int sched_getpid () {
	return current->pid;
}

unsigned int sched_getppid () {
	return current->ppid;
}

unsigned long long sched_gettick () {
	return current->cpu_time;
}

void sched_ps () {
	sigset_t block_sigset, old_sigset;

	// block all signals
	sigfillset (&block_sigset);
	if (sched_blocksigs (&block_sigset, &old_sigset) < 0) {
		fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
		fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
	}

	fprintf (stderr, "PID\tPPID\tTASK_STATE\tSTACK_BASE\tNICE\tDYN\tCPU_TIME\n");

	// print out relevant information
	struct sched_procnode * pn;
	for (pn = proc_anchor.next; pn->proc != NULL; pn = pn->next) {
		fprintf (stdout, "%04d\t", pn->proc->pid);
    fflush(stdout);
		fprintf (stdout, "%04d\t", pn->proc->ppid);
    fflush(stdout);
		switch (pn->proc->task_state) {
			case SCHED_READY:
				fprintf (stdout, "SCHED_READY\t");
				break;
			case SCHED_RUNNING:
				fprintf (stdout, "SCHED_RUNNING\t");
				break;
			case SCHED_SLEEPING:
				fprintf (stdout, "SCHED_SLEEPING\t");
				break;
			case SCHED_ZOMBIE:
				fprintf (stdout, "SCHED_ZOMBIE\t");
				break;
		}
    fflush(stdout);
		fprintf (stdout, "%x\t", pn->proc->stack_base);
    fflush(stdout);
		fprintf (stdout, "%d\t", pn->proc->nice);
    fflush(stdout);
		fprintf (stdout, "%d\t", pn->proc->priority);
    fflush(stdout);
		fprintf (stdout, "%llu\n", pn->proc->cpu_time);
    fflush(stdout);
	}
	
	// unblock and restore signals
	if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) {
		fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
		fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
	}
}

int sched_switch () {
	sigset_t block_sigset, old_sigset;

	// block all signals
	sigfillset (&block_sigset);
	if (sched_blocksigs (&block_sigset, &old_sigset) < 0) {
		fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
		fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		return -1;
	}

	// if current process is ZOMBIE and its parent is SLEEPING, wake the parent
	if (current->task_state == SCHED_ZOMBIE && current->parent->task_state == SCHED_SLEEPING) {
		printf ("\nContext switch from %d to ", current->pid); // debug info
		current = current->parent;                             // make parent the current process
		printf ("%d (waking up parent)\n", current->pid);      // debug info
		restorectx (&current->pctx, SCHED_EXIT_RET);
	}

	current->slice_max = 0; // slice_max = 0 implies current process has recently finished running
	current->slice_acc = 0; // reset the current process time slice accumulator

	int cycle_flag;
	cycle_flag = 1;
	struct sched_procnode * pn;

	// update all priorities based on nice values
	//   and check to see if there are any READY processes that have not yet run
	for (pn = proc_anchor.next; pn->proc != NULL; pn = pn->next) {
		pn->proc->priority = 19 - pn->proc->nice;

		if (pn->proc->task_state == SCHED_READY && pn->proc->slice_max != 0) {
			cycle_flag = 0;
		}
	}

	// if all the READY processes have run, we update all slice_max values;
	//   otherwise, we only update slice_max values for those processes that haven't run
	if (cycle_flag == 1) {
		for (pn = proc_anchor.next; pn->proc != NULL; pn = pn->next) {
			pn->proc->slice_max = pn->proc->priority + 1;
		}
	} else {
		for (pn = proc_anchor.next; pn->proc != NULL; pn = pn->next) {
			if (pn->proc->slice_max != 0) {
				pn->proc->slice_max = pn->proc->priority + 1;
			}
		}
	}

	int ret_flag, best_priority;
	ret_flag = 0;
	best_priority = -1;
	struct sched_proc * best_proc;
	best_proc = NULL;

	// do not save the context of a SLEEPING process again (already saved in sched_wait)
	if (current->task_state != SCHED_SLEEPING) {
		if (savectx (&current->pctx) == SCHED_SWITCH_RET) {
			ret_flag = 1;
		}
	}
	
	// the new process has not yet been scheduled; we schedule it here
	if (ret_flag == 0) {
		// loop through and find best READY process to run
		for (pn = proc_anchor.next; pn->proc != NULL; pn = pn->next) {
			// if the process is READY to be scheduled,
			// and the process has a greater priority than the best one so far,
			// and the process has not run recently, update the best parameters
			if (pn->proc->task_state == SCHED_READY	&& pn->proc->priority > best_priority && pn->proc->slice_max != 0) {
				best_priority = pn->proc->priority;
				best_proc = pn->proc;
			}
		}

		// if the best_proc is NULL, we know that there are no READY processes
		if (best_proc == NULL) {
			fprintf (stderr, "FATAL: No processes are available for scheduling! Aborting...\n");
			exit (-1);
		}
		
		// we have found the process to be scheduled; let's switch to it
		printf ("\nContext switch from %d to ", current->pid); // debug info
		current = best_proc;
		current->task_state = SCHED_RUNNING;
		printf ("%d\n", current->pid);                         // debug info
		
		// print information about all living processes (debug)
		sched_ps ();                                           // debug info
	
		// here we actually switch the context to the now RUNNING process
		restorectx (&current->pctx, SCHED_SWITCH_RET);
	} else {
		// unblock and restore signals
		if (sched_unblocksigs (&block_sigset, &old_sigset) < 0) {
			fprintf (stderr, "ERROR: Process signal mask for process %d could not be restored!\n", current->pid);
			fprintf (stderr, "--> sigprocmask() failure: %s\n", strerror (errno));
		}
	}
}

void sched_tick () {
	// only tick running processes
	if (current->task_state == SCHED_RUNNING) {
		if (current->slice_acc < current->slice_max) {
			current->cpu_time += 1;            // increment the cpu time
			current->slice_acc += 1;           // increment the time slice accumulator
		} else {
			current->task_state = SCHED_READY; // make process READY instead of RUNNING
			sched_switch ();                   // switch to new process
		}
	}
}

int sched_blocksigs (sigset_t * block_sigset, sigset_t * old_sigset) {
	// try to block all signals
	if (sigprocmask (SIG_BLOCK, block_sigset, old_sigset) < 0) return -1;

	return 0;
}

int sched_unblocksigs (sigset_t * block_sigset, sigset_t * old_sigset) {
	// try to unblock all signals
	if (sigprocmask (SIG_UNBLOCK, block_sigset, NULL) < 0) return -1;

	// unblock SIGVTALRM and SIGABRT (masked by signal handlers or otherwise)
	sigdelset (old_sigset, SIGVTALRM);
	sigdelset (old_sigset, SIGABRT);

	// try to restore old mask
	if (sigprocmask (SIG_BLOCK, old_sigset, NULL) < 0) return -1;

	return 0;
}

unsigned short int sched_getunusedpid () {
	int i;

	// check for free pids; return the first available pid
	for (i = 1; i < SCHED_NPROC + 1; ++i) {
		if (pid_table[i] == 0) {
			return i;
		}
	}
	
	// if we are here, no pids remain
	return 0;
}
