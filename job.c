#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include "job.h"

#define DEBUG3

int jobid = 0;
int siginfo = 1;
int fifo;
int globalfd;

struct waitqueue *head = NULL, *head2 = NULL, *head3 = NULL;
struct waitqueue *next = NULL, *current = NULL;

/* 调度程序 */
void scheduler() {
	struct jobinfo *newjob = NULL;
	struct jobcmd cmd;
	int count = 0;
	bzero(&cmd, DATALEN);
	if ((count = read(fifo, &cmd, DATALEN))<0)
		error_sys("read fifo failed");

	// debug info
	#ifdef DEBUG
		printf("Reading whether other process send command!\n");
		if (count)
			printf("cmd cmdtype\t%d\ncmd defpri\t%d\ncmd data\t%s\n", cmd.type, cmd.defpri, cmd.data);
		else
			printf("no data read\n");
	#endif

	#ifdef DEBUG1
		printf("before updateall():\n");
		do_stat();
	#endif

	/* 更新等待队列中的作业 */
	updateall();

	#ifdef DEBUG1
		printf("after updateall():\n");
		do_stat();
	#endif

 	#ifdef DEBUG
 		printf("Select which job to run next!\n");
 	#endif

	/* 选择高优先级作业 */
	next = jobselect();

	#ifdef DEBUG
		printf("Switch to the next job!\n");
	#endif

	/* 作业切换 */
	jobswitch();

	#ifdef DEBUG2
		if (count) {
			printf("before ENQ, DEQ, STAT:\n");
			do_stat();
		}
	#endif
	// end of debug info

	switch(cmd.type) {
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

	#ifdef DEBUG2
		if (count) {
			printf("after ENQ, DEQ, STAT:\n");
			do_stat();
		}
	#endif
}

int allocjid() {
	return ++jobid;
}

void updateall() {
	struct waitqueue *p;
	int flag;
	int i;

	if (current && current->job->state == DONE) { /* 当前作业完成 */
		/* 作业完成，删除它 */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++) {
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* 释放空间 */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	// update wait queue
	// job waiting longer than 10s should go upward
	for (p = head; p != NULL; p = p->next) {
		p->job->wait_time += 1000;
		if (p->job->wait_time >= 10000 && p->job->curpri > 1) {
			p->job->curpri--;
			p->job->wait_time = 0;
		}	
	}
	for (p = head2; p != NULL; p = p->next) {
		p->job->wait_time += 1000;
		if (p->job->wait_time >= 10000 && p->job->curpri > 1) {
			p->job->curpri--;
			p->job->wait_time = 0;
		}	
	}
	for (p = head3; p != NULL; p = p->next) {
		p->job->wait_time += 1000;
		if (p->job->wait_time >= 10000 && p->job->curpri > 1) {
			p->job->curpri--;
			p->job->wait_time = 0;
		}
	}

	// update current
	if (current) {
		// change the place of the job
		// if the corresponding timeslice is over
		flag = 0;
		if (current->job->timeslice == -1)
			flag = 1;
		else if (current->job->curpri == 3 && current->job->timeslice == 5)
			flag = 1;
		else if (current->job->curpri == 2 && current->job->timeslice == 2)
			flag = 1;
		else if (current->job->curpri == 1 && current->job->timeslice == 1)
			flag = 1;
		if (flag) {
			current->job->timeslice = 0;
			// always add to queue tail
			current->next = NULL;
			// if not in low queue, put it downward
			switch (current->job->curpri) {
				case 1:
					current->job->curpri = 2;
					if (head2) {
						for (p = head2; p->next != NULL; p = p->next);
						p->next = current;
					} else
						head2 = current;
					break;
				case 2:
					current->job->curpri = 3;
				case 3:
					if (head3) {
						for (p = head3; p->next != NULL; p = p->next);
						p->next = current;
					} else
						head3 = current;
					break;
				default:
					error_sys("wrong priority level");
			}
			kill(current->job->pid, SIGSTOP);
			current->job->wait_time = 0;
			current->job->state = READY;
			current = NULL;
		} else {
			// update time info
			current->job->run_time += 1; // represents 1000ms
			current->job->timeslice += 1;
		}
	}

}

struct waitqueue* jobselect() {
	struct waitqueue *select;

	// current running in its timeslice
	if (current)
		return current;

	// choose the first one
	// remove the selected job from the waitqueue
	if (head) {
		select = head;
		head = head->next;
	} else if (head2) {
		select = head2;
		head2 = head2->next;
	} else if (head3) {
		select = head3;
		head3 = head3->next;
	} else
		select = NULL;

	#ifdef DEBUG3
		if (select) {
			printf("Selected job:\n");
			show_job_info(select->job);
		} else
			printf("No job selected\n");
	#endif
	return select;
}

void jobswitch() {
	struct waitqueue *p;

	#ifdef DEBUG4
		printf("before jobswitch():\n");
		do_stat();
	#endif

	// current running in its timeslice
	if (current == next)
		return;

	if (next != NULL) {
		#ifdef DEBUG
			printf("switch to Pid: %d\n", next->job->pid);
		#endif
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->timeslice = 0;
		kill(current->job->pid, SIGCONT);
		return; 
		#ifdef DEBUG4
			printf("after jobswitch() switches the job:\n");
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
			scheduler();
			return;
		case SIGCHLD: /* 子进程结束时传送给父进程的信号 */
			ret = waitpid(-1, &status, WNOHANG);
			if (ret == 0)
				return;
			if (WIFEXITED(status)) {
				current->job->state = DONE;
				printf("normal termation, exit status = %d\n", WEXITSTATUS(status));
			}else if (WIFSIGNALED(status)) {
				printf("abnormal termation, signal number = %d\n", WTERMSIG(status));
			}else if (WIFSTOPPED(status)) {
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
	int exec_rt_code;
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
	newjob->timeslice = 0;
	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));
	newjob->cmdarg = arglist;
	offset = enqcmd.data;
	argvec = enqcmd.data;
	while (i < enqcmd.argnum) {
		if (*offset == ':') {
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

	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));
	newnode->job = newjob;
	newnode->next = NULL;

	// preempty
	/*为作业创建进程*/
	if ((pid = fork())<0)
		error_sys("enq fork failed");

	if (pid == 0) { // child process
		newjob->pid = getpid();

		// raise(SIGSTOP);
		#ifdef DEBUG
			printf("begin running\n");
			for(i = 0;arglist[i]!= NULL;i++)
				printf("arglist %s\n", arglist[i]);
		#endif

		/*复制文件描述符到标准输出*/
		dup2(globalfd, 1);

		/* 执行命令 */
		if ((exec_rt_code = execv(arglist[0], arglist))<0)
			printf("exec failed: %d\n", exec_rt_code);
		exit(1); // dead code?
	} else {
		newjob->pid = pid;
		// set as a timeslice end flag
		if (current)
			current->job->timeslice = -1;
		updateall();
		next = newnode;
		jobswitch();
		#ifdef DEBUG3
			printf("New job:\n");
			show_job_info(current->job);
		#endif
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
	if (current && current->job->jid == deqid) {
		printf("teminate current job\n");
		kill(current->job->pid, SIGKILL);
		for(i = 0;(current->job->cmdarg)[i]!= NULL;i++) {
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
		if (head) {
			for(prev = head, p = head;p!= NULL;prev = p, p = p->next)
				if (p->job->jid == deqid) {
					select = p;
					selectprev = prev;
					break;
				}
				selectprev->next = select->next;
				if (select == selectprev)
					head = NULL;
		}
		if (head2) {
			for(prev = head2, p = head2;p!= NULL;prev = p, p = p->next)
				if (p->job->jid == deqid) {
					select = p;
					selectprev = prev;
					break;
				}
				selectprev->next = select->next;
				if (select == selectprev)
					head = NULL;
		}
		if (head3) {
			for(prev = head3, p = head3;p!= NULL;prev = p, p = p->next)
				if (p->job->jid == deqid) {
					select = p;
					selectprev = prev;
					break;
				}
				selectprev->next = select->next;
				if (select == selectprev)
					head = NULL;
		}
		if (select) {
			for(i = 0;(select->job->cmdarg)[i]!= NULL;i++) {
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

	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tQUEUE\t\n");
	printf("%d\t%d\t%d\t%d\t%d\t%s\t%d\n", 
		job->jid, 
		job->pid, 
		job->ownerid, 
		job->run_time, 
		job->wait_time, 
		timebuf,
		job->curpri);
}

void do_stat_to_fifo() {
	struct waitqueue *p;
	char timebuf[BUFLEN];
	char fifobuf[FIFOLEN];
	char tmp[BUFLEN];
	int fifo;

	sprintf(fifobuf, "JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\tQUEUE\n");
	if (current) {
		strcpy(timebuf, ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		sprintf(tmp, "%d\t%d\t%d\t%d\t%d\t%s\t%s\t%d\n", 
			current->job->jid, 
			current->job->pid, 
			current->job->ownerid, 
			current->job->run_time, 
			current->job->wait_time, 
			timebuf, "RUNNING", current->job->curpri);
		strcat(fifobuf, tmp);
	}

	for(p = head;p!= NULL;p = p->next) {
		strcpy(timebuf, ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		sprintf(tmp, "%d\t%d\t%d\t%d\t%d\t%s\t%s\t%d\n", 
			p->job->jid, 
			p->job->pid, 
			p->job->ownerid, 
			p->job->run_time, 
			p->job->wait_time, 
			timebuf, "READY", p->job->curpri);
		strcat(fifobuf, tmp);
	}
	for(p = head2;p!= NULL;p = p->next) {
		strcpy(timebuf, ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		sprintf(tmp, "%d\t%d\t%d\t%d\t%d\t%s\t%s\t%d\n", 
			p->job->jid, 
			p->job->pid, 
			p->job->ownerid, 
			p->job->run_time, 
			p->job->wait_time, 
			timebuf, "READY", p->job->curpri);
		strcat(fifobuf, tmp);
	}
	for(p = head3;p!= NULL;p = p->next) {
		strcpy(timebuf, ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		sprintf(tmp, "%d\t%d\t%d\t%d\t%d\t%s\t%s\t%d\n", 
			p->job->jid, 
			p->job->pid, 
			p->job->ownerid, 
			p->job->run_time, 
			p->job->wait_time, 
			timebuf, "READY", p->job->curpri);
		strcat(fifobuf, tmp);
	}

	if ((fifo = open("/tmp/stat", O_WRONLY))<0)
		error_sys("open fifo failed");
	// write() is atomic
	if (write(fifo, fifobuf, FIFOLEN)<0)
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
	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\t\tQUEUE\n");
	if (current) {
		strcpy(timebuf, ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\t\t%d\n", 
			current->job->jid, 
			current->job->pid, 
			current->job->ownerid, 
			current->job->run_time, 
			current->job->wait_time, 
			timebuf, "RUNNING", current->job->curpri);
	}

	for(p = head;p!= NULL;p = p->next) {
		strcpy(timebuf, ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\t\t%d\n", 
			p->job->jid, 
			p->job->pid, 
			p->job->ownerid, 
			p->job->run_time, 
			p->job->wait_time, 
			timebuf, "READY", p->job->curpri);
	}
	for(p = head2;p!= NULL;p = p->next) {
		strcpy(timebuf, ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\t\t%d\n", 
			p->job->jid, 
			p->job->pid, 
			p->job->ownerid, 
			p->job->run_time, 
			p->job->wait_time, 
			timebuf, "READY", p->job->curpri);
	}
	for(p = head3;p!= NULL;p = p->next) {
		strcpy(timebuf, ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1] = '\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\t\t%d\n", 
			p->job->jid, 
			p->job->pid, 
			p->job->ownerid, 
			p->job->run_time, 
			p->job->wait_time, 
			timebuf, "READY", p->job->curpri);
	}
}

int main() {
	struct timeval interval;
	struct itimerval new, old;
	struct stat statbuf;
	struct sigaction newact, oldact1, oldact2;

	#ifdef DEBUG
		printf("DEBUG is open!\n");
	#endif
	if (stat("/tmp/server", &statbuf) == 0) {
		if (remove("/tmp/server")<0)
			error_sys("remove failed");
	}
	if (stat("/tmp/stat", &statbuf) == 0) {
		if (remove("/tmp/stat")<0)
			error_sys("remove failed");
	}

	if (mkfifo("/tmp/server", 0666)<0)
		error_sys("mkfifo failed");
	if (mkfifo("/tmp/stat", 0666)<0)
		error_sys("mkfifo failed");

	/* 在非阻塞模式下打开FIFO */
	if ((fifo = open("/tmp/server", O_RDONLY|O_NONBLOCK))<0)
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
