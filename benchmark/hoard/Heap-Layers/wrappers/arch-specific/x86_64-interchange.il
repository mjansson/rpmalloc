.inline MyInterlockedExchange,0
 
movq %rsi,%rax
lock
xchgq %rax,(%rdi)

.end
