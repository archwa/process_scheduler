# process scheduler

A simulation of the Unix process environment to demonstrate the functionality
of a process scheduler.

## Description

Processes on a given operating system are placed on the processor (assuming the
machine is of a uni-processor variety, i.e., single core) one at a time by the
operating system's scheduler.  The amount of time a process has to run on the
processor is determined by a number of factors, including the nice value,
dynamic priority, et. al. of that process.

This scheduler test bed simulates a single processor environment with multiple
processes that share the same virtual address space for various process stacks.
Multiple processes can be `sched_fork()`'d within the test environment. These
pseudo-processes can be assigned new nice values using `sched_nice()`.  Once a
process has finished running, `sched_exit()` can be called.  Parent processes
can `sched_wait()` for zombie or recently-terminated children.

The `sched_switch()` function is called whenever one process switches to
another, for example after `sched_exit()` is called or whenever the scheduler
decides that a process has been on the processor for long enough.  The
scheduler is notified via the interval `sched_tick()` command when the max
timeslice of a given process has been reached.  The `sched_switch()` function
uses a fairly straightforward algorithm (detailed in the code) that determines
which process is best-fit for running next on the processor.  It then hands
execution over to that process.

The `main` scheduler test bed can be easily tweaked to simulate different
process environments with different processes and properties (e.g. nice
values, number of processes, behavior, etc.).

Please note that this particular implementation is supported exclusively by
amd64 machines.


More details are given in the `doc/` directory.

## Dependencies

* gcc
* make

## Compilation

To build the `main` scheduler test bed, simply do the following:

	make

## Usage

To run the scheduler test bed, do:

	make run
