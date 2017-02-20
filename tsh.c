/* 
 * tsh - A tiny shell program with job control
 * 
 * Vaibhav Patel
   Id: 201401222
   Email Id: 201401222@daiict.ac.in
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <math.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline); 
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);
void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{

	pid_t pidc;
	char **argv;
	argv=(char**)malloc(MAXLINE);  // making string array for storing command line arguments
	int isBG=parseline(cmdline,argv); // now splitting cmdline into arguments 
	// isBG is 1 if the last argument is &
	// otherwise 0
	if(argv[0]==NULL) // if cmdline has only white space or nothing at all
		return;
	int isBuiltIn=builtin_cmd(argv); 
	// isBuiltIn is 1 if it is built in command;
	if(isBuiltIn==1) 
		return ;
	// non-built in command will execute below
	if(isBuiltIn==0)
	{
		sigset_t sets;
		sigemptyset(&sets);
		sigaddset(&sets,SIGCHLD);  // this is a critical part of the programme so SIGCHLD has to be blocked
		sigprocmask(SIG_BLOCK,&sets,NULL);
		if((pidc=fork())==0)  // doing fork exec so shell  does not die
		{
			setpgrp();
			sigprocmask(SIG_UNBLOCK,&sets,NULL);
			execvp(argv[0],argv); // execing the process
			printf("%s : Command not found\n",argv[0]); // if the exec fails
			exit(0);
		}
		if(isBG==1)
		{
			// adding job in jobs array as a background job
			addjob(jobs,pidc,BG,cmdline);
			printf("[%d] (%d) %s",pid2jid(pidc),pidc,cmdline);
			sigprocmask(SIG_UNBLOCK,&sets,NULL);
		}
		if(!isBG)
		{
			// foreground job
			addjob(jobs,pidc,FG,cmdline);
			sigprocmask(SIG_UNBLOCK,&sets,NULL);
			waitfg(pidc);	
		}
	}
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user haint i=1;int jobid=0;
	for(;i<strlen(argv[1]);i++){
	jobid+=argv[1][i]*pow(10,strlen(argv[1]-1));
	}
	printf("%d\n",jobid);
	struct job_t *getjobjid(jobs, jobid); s requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
	static char array[MAXLINE]; /* holds local copy of command line */
	char *buf = array;          /* ptr that traverses command line */
	char *delim;                /* points to first space delimiter */
	int argc;                   /* number of args */
	int bg;                     /* background job? */

	strcpy(buf, cmdline);
	buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
	while (*buf && (*buf == ' ')) /* ignore leading spaces */
		buf++;

	/* Build the argv list */
	argc = 0;
	if (*buf == '\'') {
		buf++;
		delim = strchr(buf, '\'');
	}
	else {
		delim = strchr(buf, ' ');
	}

	while (delim) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
			buf++;

		if (*buf == '\'') {
			buf++;
			delim = strchr(buf, '\'');
		}
		else {
			delim = strchr(buf, ' ');
		}
	}
	argv[argc] = NULL;

	if (argc == 0)  /* ignore blank line */
		return 1;

	/* should the job run in the background? */
	if ((bg = (*argv[argc-1] == '&')) != 0) {
		argv[--argc] = NULL;
	}
	return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{	
	if(strcmp(argv[0],"quit")==0)
	{
		int i=0;
		int stoppedJobs=0;
		for(;i<MAXJOBS;i++) 
		{
			if (jobs[i].state==ST)
			{
				stoppedJobs=1;
				break;
			}
		}

		if(stoppedJobs==1)
		{
			printf("There are stopped of background processes , you can't quit.\n");
			return 1;
		}
		else if(stoppedJobs==0)
		{
			exit(0);			
		}
	}

	if(strcmp(argv[0],"jobs")==0)
	{
		listjobs(jobs);
		return 1;
	}
	if((strcmp(argv[0],"fg")==0)||(strcmp(argv[0],"bg")==0))
	{
		do_bgfg(argv);
		return 1;
	}
    return 0;     /* not a builtin command  */
}


/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */

void do_bgfg(char **argv) 
{
	int i = 0, jorp;
	int errorChecker=0;
    struct job_t *job = NULL;
	 if (argv[1] == NULL) // command line has no other argument other than fg or bg
    {
        char c='%';
		if((strcmp(argv[0],"bg")==0))
			printf("bg command requires PID or %cjobid argument\n",c);
		else
			printf("fg command requires PID or %cjobid argument\n",c); 
        return;
    }
    if (argv[1][0] == '%') //if the first character is % than the to change the starting point of the string for checking
    {
        i++;
    }
    for (; argv[1][i] != '\0'; i++) // checking if it has no othe character other than integer 
        if (!(argv[1][i] >= '0' && argv[1][i] <= '9'))
        {
            errorChecker=1;
            break;
        }
        // errorChecker is 1 if any eroor is present
    if (errorChecker) 
    {
    	// if any error then return
    	 char c='%';
		if((strcmp(argv[0],"bg")==0))
			printf("bg: argument must be a PID or %cjobid\n",c);
		else 
			printf("fg: argument must be a PID or %cjobid\n",c);
		return; 
    }
    else
    { // if there is no error then 
    	// changing a string to integer with the help of atoi function
    	if (argv[1][0] == '%')
        {    
        	char *arg=argv[1];
        	arg++; // incrementing the strinc pointer because we don't need '%' now
        	jorp=atoi(arg);
        }
        else
        {
        	jorp = atoi(argv[1]); 
        }
// now the variable jorp has the integer which can be of a job or a process depending upon the first character of argv[1]
       if (argv[1][0] != '%')
            job = getjobpid(jobs, jorp);
        else
            job = getjobjid(jobs, jorp);
        //getting pointer of the struct job_t by jobid or process id ,whichever is applicable 
    }

    if (!errorChecker) // finally there is no error in cmdline now. 
    {
        if (job == NULL) //checking if the job or process is or is not in job table
        {
            if (argv[1][0] == '%')
                printf("%s %c%d: No such job\n", argv[0], '%', jorp);
            else
                printf("(%d): No such process\n", jorp);
            return;
        }
        //now finally all the catches and errors are passed 
        // the commanf is acceptable 
        // so now we do the fg bg job
        if (strcmp(argv[0], "fg") == 0) // if the command is fg -> forgrounding the job  
        {
            if (job->state == ST) // if it is stopped then forground it
            {
                job->state = FG;
                kill((-1) * job->pid, SIGCONT);
                waitfg(job->pid);
            }
            else if (job->state == BG)
            {
                job->state = FG; //if it is background then forgrounding it
                waitfg(job->pid);
            }
        }
        else //if the command is bg
        {
            kill(-job->pid, SIGCONT); 
            job->state = BG; //changing its state to background
            printf("[%d] (%d) %s", job->jid, job->pid, (char *) (job->cmdline));
        }

    }

    return;
}
	/* 
	 * waitfg - Block until process pid is no longer the foreground process
	 */
void waitfg(pid_t pid)
{	
	struct job_t *jidc=getjobpid(jobs,pid);  
	for(; (*jidc).state==FG ;)  // blocking shell prompt until the forground process is still forground 
	{
		sleep(1);
	}
	return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{	
	int stat;
	pid_t pid;
	while((pid=waitpid(-1,&stat,WNOHANG|WUNTRACED))>0) // not waiting to finish the child
		// but if any child is finished .and a zombie theen to reap it.
	{
		if(WIFEXITED(stat)!=0)
		{
			deletejob(jobs,pid); // now theere is no need for keeping it in job table
		}
		else if(WIFSIGNALED(stat)!=0) // printing the child's terminating cause
		{
			printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(pid),pid,WTERMSIG(stat));
			deletejob(jobs,pid);
		}
		else if(WIFSTOPPED(stat)!=0)  // printing if the child is stopped
		{
			struct job_t *jobid=getjobpid(jobs,pid);
			jobid->state=ST;
			printf("Job [%d] (%d) stopped by signal %d\n",pid2jid(pid),pid,WSTOPSIG(stat));
		}
	}
	return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
	pid_t forG=fgpid(jobs);
	kill(-forG,SIGINT);  // send SIGINT signal to all the processes with the process group forG 
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{  	
	pid_t forG=fgpid(jobs);
	kill(-forG,SIGTSTP);// send SIGTSTP signal to all the processes with the process group forG 
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
	job->pid = 0;
	job->jid = 0;
	job->state = UNDEF;
	job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
	int i, max=0;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid > max)
			max = jobs[i].jid;
	return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
	int i;
	
	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == 0) {
			jobs[i].pid = pid;
			jobs[i].state = state;
			jobs[i].jid = nextjid++;
			if (nextjid > MAXJOBS)
				nextjid = 1;
			strcpy(jobs[i].cmdline, cmdline);
			if(verbose){
				printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
				listjobs(jobs);
			}
			return 1;
		}
	}
	printf("Tried to create too many jobs\n");
	return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == pid) {
			clearjob(&jobs[i]);
			nextjid = maxjid(jobs)+1;
			return 1;
		}
	}
	return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].state == FG)
			return jobs[i].pid;
	return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
	int i;

	if (pid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid){
			return &jobs[i];
		}
	return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
	int i;

	if (jid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid == jid)
			return &jobs[i];
	return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
	int i;

	if (pid < 1)
		return 0;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid) {
			return jobs[i].jid;
		}
	return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
	int i;	
	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid != 0) {
			printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
			switch (jobs[i].state) {
				case BG: 
					printf("Running ");
					break;
				case FG: 
					printf("Foreground ");
					break;
				case ST: 
					printf("Stopped ");
					break;
				default:
					printf("listjobs: Internal error: job[%d].state=%d ", 
							i, jobs[i].state);
			}
			printf("%s", jobs[i].cmdline);
		}
	}
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
	printf("Usage: shell [-hvp]\n");
	printf("   -h   print this message\n");
	printf("   -v   print additional diagnostic information\n");
	printf("   -p   do not emit a command prompt\n");
	exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
	fprintf(stdout, "%s\n", msg);
	exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
	struct sigaction action, old_action;

	action.sa_handler = handler;  
	sigemptyset(&action.sa_mask); /* block sigs of type being handled */
	action.sa_flags = SA_RESTART; /* restart syscalls if possible */

	if (sigaction(signum, &action, &old_action) < 0)
		unix_error("Signal error");
	return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
	printf("Terminating after receipt of SIGQUIT signal\n");
	exit(1);
}




