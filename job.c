#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "job.h"

//#define DEBUG

int jobid = 0;
int siginfo = 1;
int fifo;
int globalfd;

// second count of queue
int mid_count, low_count;

struct waitqueue *head = NULL, *head2 = NULL, *head3 = NULL;
struct waitqueue *next = NULL, *current = NULL;

int job_count() {
	struct waitqueue *p;
	int rt = 0;
	if (current)
		rt++;
	if (next)
		rt++;
	for (p = head; p != NULL; p = p->next)
		rt++;
	return rt;
}

/* 调度程序 */
void scheduler() {
	struct jobinfo *newjob = NULL;
	struct jobcmd cmd;
	int count = 0;
	bzero(&cmd, DATALEN);
	if((count = read(fifo, &cmd, DATALEN))<0)
		error_sys("read fifo failed");

	if (!mid_count)
		printf("mid\n");
	if (!low_count)
		printf("low\n");

	// debug info
	#ifdef DEBUG
		printf("Reading whether other process send command!\n");
		if (count)
			printf("cmd cmdtype\t%d\ncmd defpri\t%d\ncmd data\t%s\n", cmd.type, cmd.defpri, cmd.data);
	#endif
	#ifdef DEBUG
		printf("Update jobs in wait queue!\n");
	#endif
	#ifdef DEBUG1
		printf("before updateall():\n");
		do_stat();
	#endif

	/* 更新等待队列中的作业 */
	updateall();
	//printf("%d jobs updated!\n", job_count());

	#ifdef DEBUG1
		printf("after updateall():\n");
		do_stat();
	#endif
	#ifdef DEBUG2
		printf("before ENQ, DEQ, STAT:\n");
		do_stat();
	#endif
	// end of debug info

	switch(cmd.type){
		case ENQ:
			#ifdef DEBUG
				printf("Execute enq!\n");
			#endif
			do_enq(newjob, cmd);
			break;
		case DEQ:
			#ifdef DEBUG
				printf("Execute deq!\n");
			#endif
			do_deq(cmd);
			break;
		case STAT:
			#ifdef DEBUG
				printf("Execute stat!\n");
			#endif
			do_stat_to_fifo();
			break;
		default:
			break;
	} 

	// debug info
	#ifdef DEBUG2
		printf("after ENQ, DEQ, STAT:\n");
		do_stat();
	#endif
 	#ifdef DEBUG
 		printf("Select which job to run next!\n");
 	#endif
 	// end of debug info

	/* 选择高优先级作业 */
	next = jobselect();
	printf("%d jobs after jobselect()\n", job_count());

	#ifdef DEBUG
		printf("Switch to the next job!\n");
	#endif

	/* 作业切换 */
	jobswitch();
	//printf("%d jobs after jobswitch()\n", job_count());
}

int allocjid() {
	return ++jobid;
}

void upgrade_pri() {

}

void updateall() {
	struct waitqueue *p;

	/* 更新作业运行时间 */
	if(current)
		current->job->run_time += 1; /* 加1代表1000ms */

	/* 更新作业等待时间及优先级 */
	for(p = head; p != NULL; p = p->next){
		printf("%d\n", p->job->jid);
		p->job->wait_time += 1000;
		if(p->job->wait_time >= 5000 && p->job->curpri < 3){
			p->job->curpri++;
			p->job->wait_time = 0;
		}
	}
}

struct waitqueue* jobselect() {
	struct waitqueue *p, *prev, *select, *selectprev;
	int highest;

	if(head){
		// choose the first one
		select = head;
		selectprev = NULL;
		highest = select->job->curpri;
		/* 遍历等待队列中的作业，找到优先级最高的作业 */
		for(p = head->next, prev = NULL; p != NULL; p = p->next) {
			if(p->job->curpri > highest){
				select = p;
				selectprev = prev;
				highest = p->job->curpri;
			}
			prev = p;
		}
		// remove the selected job from the waitqueue
		if (selectprev != NULL)
			selectprev->next = select->next; // do not free the node
		else
			head = head->next;
		select->next = NULL;
	} else
		select = NULL;
	#ifdef DEBUG3
		if (select) {
			printf("Selected job:\n");
			show_job_info(select->job);
		}
	#endif
	return select;
}

void jobswitch() {
	struct waitqueue *p;
	int i;

	#ifdef DEBUG4
		printf("before jobswitch():\n");
		do_stat();
	#endif
	if(current && current->job->state == DONE){ /* 当前作业完成 */
		/* 作业完成，删除它 */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* 释放空间 */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	if(next == NULL && current == NULL) {/* 没有作业要运行 */
		#ifdef DEBUG4
			printf("No job to run!\n");
		#endif
		return;
	}	else if (next != NULL && current == NULL){ /* 开始新的作业 */
		#ifdef DEBUG
			printf("begin start new job\n");
		#endif
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		kill(current->job->pid, SIGCONT);
		#ifdef DEBUG4
			printf("after jobswitch() starts a new job:\n");
			do_stat();
		#endif
		return;
	}
	else if (next != NULL && current != NULL){ /* 切换作业 */
		#ifdef DEBUG
			printf("switch to Pid: %d\n", next->job->pid);
		#endif
		kill(current->job->pid, SIGSTOP);
		current->job->curpri = current->job->defpri;
		current->job->wait_time = 0;
		current->job->state = READY;

		/* 放回等待队列 */
		if(head){
			for(p = head; p->next != NULL; p = p->next);
			p->next = current;
		}else{
			head = current;
		}
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->wait_time = 0;
		kill(current->job->pid, SIGCONT);

		#ifdef DEBUG4
			printf("after jobswitch() switches the job:\n");
			do_stat();
		#endif
		return;
	}else{ /* next == NULL且current != NULL，不切换 */
		#ifdef DEBUG4
			printf("after jobswitch() doesn't switch:\n");
			do_stat();
		#endif
		return;
	}
}

void sig_handler(int sig, siginfo_t *info, void *notused) {
	int status;
	int ret;

	switch (sig) {
		case SIGVTALRM: /* 到达计时器所设置的计时间隔 */
			#ifdef DEBUG
				printf("SIGVTALRM received!\n");
			#endif
			mid_count = (mid_count + 1) % 2;
			low_count = (low_count + 1) % 5;
			scheduler();
			return;
		case SIGCHLD: /* 子进程结束时传送给父进程的信号 */
			ret = waitpid(-1, &status, WNOHANG);
			if (ret == 0)
				return;
			if(WIFEXITED(status)){
				current->job->state = DONE;
				printf("normal termation, exit status = %d\n", WEXITSTATUS(status));
			}else if (WIFSIGNALED(status)){
				printf("abnormal termation, signal number = %d\n", WTERMSIG(status));
			}else if (WIFSTOPPED(status)){
				printf("child stopped, signal number = %d\n", WSTOPSIG(status));
			}
			#ifdef DEBUG5
				do_stat();
			#endif
			return;
		default:
			return;
	}
}

void do_enq(struct jobinfo *newjob, struct jobcmd enqcmd) {
	struct waitqueue *newnode, *p;
	int i = 0, pid;
	char *offset, *argvec, *q;
	char **arglist;
	sigset_t zeromask;

	sigemptyset(&zeromask);

	/* 封装jobinfo数据结构 */
	newjob = (struct jobinfo *)malloc(sizeof(struct jobinfo));
	newjob->jid = allocjid();
	newjob->defpri = enqcmd.defpri;
	newjob->curpri = enqcmd.defpri;
	newjob->ownerid = enqcmd.owner;
	newjob->state = READY;
	newjob->create_time = time(NULL);
	newjob->wait_time = 0;
	newjob->run_time = 0;
	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));
	newjob->cmdarg = arglist;
	offset = enqcmd.data;
	argvec = enqcmd.data;
	while (i < enqcmd.argnum){
		if(*offset == ':'){
			*offset++ = '\0';
			q = (char*)malloc(offset - argvec);
			strcpy(q, argvec);
			arglist[i++] = q;
			argvec = offset;
		}else
			offset++;
	}

	arglist[i] = NULL;

#ifdef DEBUG

	printf("enqcmd argnum %d\n", enqcmd.argnum);
	for(i = 0;i < enqcmd.argnum; i++)
		printf("parse enqcmd:%s\n", arglist[i]);

#endif

	/*向等待队列中增加新的作业*/
	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));
	newnode->next = NULL;
	newnode->job = newjob;

	if(head)
	{
		for(p = head;p->next != NULL; p = p->next);
		p->next = newnode;
	}else
		head = newnode;

	/*为作业创建进程*/
	if((pid = fork())<0)
		error_sys("enq fork failed");

	if(pid == 0){
		newjob->pid = getpid();
		/*阻塞子进程, 等等执行*/
		raise(SIGSTOP);
#ifdef DEBUG

		printf("begin running\n");
		for(i = 0;arglist[i]!= NULL;i++)
			printf("arglist %s\n", arglist[i]);
#endif

		/*复制文件描述符到标准输出*/
		dup2(globalfd, 1);
		/* 执行命令 */
		if(execv(arglist[0], arglist)<0)
			printf("exec failed\n");
		exit(1);
	}else{
		newjob->pid = pid;
	}
}

void do_deq(struct jobcmd deqcmd) {
	int deqid, i;
	struct waitqueue *p, *prev, *select, *selectprev;
	deqid = atoi(deqcmd.data);

#ifdef DEBUG
	printf("deq jid %d\n", deqid);
#endif

	/*current jodid == deqid, 终止当前作业*/
	if (current && current->job->jid == deqid){
		printf("teminate current job\n");
		kill(current->job->pid, SIGKILL);
		for(i = 0;(current->job->cmdarg)[i]!= NULL;i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		free(current->job->cmdarg);
		free(current->job);
		free(current);
		current = NULL;
	}
	else{ /* 或者在等待队列中查找deqid */
		select = NULL;
		selectprev = NULL;
		if(head){
			for(prev = head, p = head;p!= NULL;prev = p, p = p->next)
				if(p->job->jid == deqid){
					select = p;
					selectprev = prev;
					break;
				}
				selectprev->next = select->next;
				if(select == selectprev)
					head = NULL;
		}
		if(select){
			for(i = 0;(select->job->cmdarg)[i]!= NULL;i++){
				free((select->job->cmdarg)[i]);
				(select->job->cmdarg)[i] = NULL;
			}
			free(select->job->cmdarg);
			free(select->job);
			free(select);
			select = NULL;
		}
	}
}

void show_job_info(struct jobinfo *job) {
	char timebuf[BUFLEN];
	strcpy(timebuf, ctime(&(job->create_time)));
	timebuf[strlen(timebuf)-1] = '\0';

	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\t");
	printf("%d\t%d\t%d\t%d\t%d\t%s\t\n", 
		job->jid, 
		job->pid, 
		job->ownerid, 
		job->run_time, 
		job->wait_time, 
		timebuf);
}

void do_stat_to_fifo() {
	struct waitqueue *p;
	char timebuf[BUFLEN];
	char fifobuf[FIFOLEN];
	char tmp[BUFLEN];
	int fifo;

	sprintf(fifobuf, "JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
	if(current){
		strcpy(timebuf, ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		sprintf(tmp, "%d\t%d\t%d\t%d\t%d\t%s\t%s\n", 
			current->job->jid, 
			current->job->pid, 
			current->job->ownerid, 
			current->job->run_time, 
			current->job->wait_time, 
			timebuf, "RUNNING");
		strcat(fifobuf, tmp);
	}

	for(p = head;p!= NULL;p = p->next){
		strcpy(timebuf, ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		sprintf(tmp, "%d\t%d\t%d\t%d\t%d\t%s\t%s\n", 
			p->job->jid, 
			p->job->pid, 
			p->job->ownerid, 
			p->job->run_time, 
			p->job->wait_time, 
			timebuf, 
			"READY");
		strcat(fifobuf, tmp);
	}

	if((fifo = open("/tmp/stat", O_WRONLY))<0)
		error_sys("open fifo failed");
	// write() is atomic
	if(write(fifo, fifobuf, FIFOLEN)<0)
		error_sys("write fifo failed");
	close(fifo);
}

void do_stat() {
	struct waitqueue *p;
	char timebuf[BUFLEN];
	/*
	*打印所有作业的统计信息:
	*1.作业ID
	*2.进程ID
	*3.作业所有者
	*4.作业运行时间
	*5.作业等待时间
	*6.作业创建时间
	*7.作业状态
	*/

	/* 打印信息头部 */
	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
	if(current){
		strcpy(timebuf, ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n", 
			current->job->jid, 
			current->job->pid, 
			current->job->ownerid, 
			current->job->run_time, 
			current->job->wait_time, 
			timebuf, "RUNNING");
	}

	for(p = head;p!= NULL;p = p->next){
		strcpy(timebuf, ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n", 
			p->job->jid, 
			p->job->pid, 
			p->job->ownerid, 
			p->job->run_time, 
			p->job->wait_time, 
			timebuf, 
			"READY");
	}
}

int main() {
	struct timeval interval;
	struct itimerval new, old;
	struct stat statbuf;
	struct sigaction newact, oldact1, oldact2;

	mid_count = 0;
	low_count = 0;

	#ifdef DEBUG
		printf("DEBUG is open!\n");
	#endif
	if(stat("/tmp/server", &statbuf) == 0){
		if(remove("/tmp/server")<0)
			error_sys("remove failed");
	}
	if(stat("/tmp/stat", &statbuf) == 0){
		if(remove("/tmp/stat")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server", 0666)<0)
		error_sys("mkfifo failed");
	if(mkfifo("/tmp/stat", 0666)<0)
		error_sys("mkfifo failed");

	/* 在非阻塞模式下打开FIFO */
	if((fifo = open("/tmp/server", O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");

	/* 建立信号处理函数 */
	newact.sa_sigaction = sig_handler;
	sigemptyset(&newact.sa_mask);
	newact.sa_flags = SA_SIGINFO;
	sigaction(SIGCHLD, &newact, &oldact1);
	sigaction(SIGVTALRM, &newact, &oldact2);

	/* 设置时间间隔为1000毫秒 */
	interval.tv_sec = 1;
	interval.tv_usec = 0;

	new.it_interval = interval;
	new.it_value = interval;
	setitimer(ITIMER_VIRTUAL, &new, &old);

	while(siginfo == 1);

	close(fifo);
	close(globalfd);
	return 0;
}
