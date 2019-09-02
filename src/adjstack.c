#include <stdio.h>

/*adjstack takes a stack which is bounded by addresses lim0 and lim1
 *and adjusts the saved frame pointers (ebp/rbp) by adj bytes.
 *It is intended to be used in conjunction with sched_fork to fix
 *the child's stack, and should be called from the parent task, after
 *having allocated the child's stack and copying into it the contents of
 *the entire parent's stack.  There are some debugging fprintfs that
 *could be commented out if needed.  This will work with either 32 or 64
 *bit code since the C compiler will adjust its notion of the size of a 
 *pointer accordingly.  The parameter adj is added to each of the saved
 *base pointers in the stack frame chain, so it should be set to the difference
 *between the original stack and the new stack
 */

void adjstack(void *lim0,void *lim1,unsigned long adj)
{
 void **p;
 void *prev,*new;
#ifdef _LP64
       __asm__(
	"movq   %%rbp,%0"
	:"=m" (p));
#else
       __asm__(
	"movl   %%ebp,%0"
	:"=m" (p));
#endif
	/* Now current bp (for adjstack fn) is in p */
	/* Unwind stack to get to saved ebp addr of caller */
	/* then begin adjustment process */
	//fprintf(stderr,"Asked to adjust child stack by %#lX bytes bet %p and %p\n",adj,lim0,lim1);
	prev=*p;
	p= prev + adj;
	for(;;)
	{
		prev=*p;
		new=prev+adj;
		if (new<lim0 || new>lim1)
		{
			/*fprintf(stderr,"Enough already, saved BP @%p is %p\n",
						p,prev);*/
			break;
		}
		*p=new;
		/*fprintf(stderr,"Adjusted saved bp @%p to %p\n",
				p,*p);*/
		p=new;
	}
}
		
