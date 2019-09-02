# 1 "src/savectx64.S"
# 1 "<built-in>"
# 1 "<command-line>"
# 31 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3 4
# 32 "<command-line>" 2
# 1 "src/savectx64.S"
# 1 "src/jmpbuf-offsets64.h" 1
# 2 "src/savectx64.S" 2

 .text
 .global savectx
 .global restorectx



savectx:

 movq %rbx, (0*8)(%rdi)
 movq %rbp, (1*8)(%rdi)
 movq %r12, (2*8)(%rdi)
 movq %r13, (3*8)(%rdi)
 movq %r14, (4*8)(%rdi)
 movq %r15, (5*8)(%rdi)
 leaq 8(%rsp), %rdx
 movq %rdx, (6*8)(%rdi)
 movq 0(%rsp), %rax
 movq %rax, (7*8)(%rdi)
 xorl %eax,%eax
 retq



restorectx:
 movq (7*8)(%rdi),%rdx

 movq (0*8)(%rdi),%rbx
 movq (2*8)(%rdi),%r12
 movq (3*8)(%rdi),%r13
 movq (4*8)(%rdi),%r14
 movq (5*8)(%rdi),%r15
 movq (0*8)(%rdi),%rbx
 movq (1*8)(%rdi),%rbp
 movq (6*8)(%rdi),%rsp
 movl %esi, %eax

 jmpq *%rdx
