.inline MyInterlockedExchange,8
 
mov %o0,%o2
mov %o1,%o0
swap [%o2],%o0
 
.end
