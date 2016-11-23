.inline MyInterlockedExchange,0
 
movl (%esp),%ecx
movl 4(%esp),%eax
lock
xchgl %eax,(%ecx)
 
.end
