job: job.c job.h error.c enq deq stat Demo
	cc -g -o job job.c job.h error.c
enq: enq.c job.h error.c
	cc -g -o enq enq.c job.h error.c
deq: deq.c job.h error.c
	cc -g -o deq deq.c job.h error.c
stat: stat.c job.h error.c
	cc -g -o stat stat.c job.h error.c
Demo: Demo.c
	cc -o Demo Demo.c
clean:
	rm job enq deq stat Demo
