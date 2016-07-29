/**************************************************
* Joshua Reed
* OregonState EECS
* Operating Systems CS 344
* Winter 2015
*
* Program: smallsh.c
*
* Description: This program is a simplified unix
* shell capable of both executing built in commands: exit,
* cd, and status, and fully independent programs. 
*
* Motivation: This program is an assignment for the 
* operating systems course at OSU. The goal is to create
* an interactive shell with basic functionality in the C
* programming language. This assignment teaches several
* unix api functions, and basics of OS interaction.
**************************************************/
#include <stdio.h>  // Manipulate file and I/O streams: printf(), fprintf(), sprintf(),fputs(), fgets(), etc...
#include <stdlib.h> // atoi(), exit(), qsort(), rand(), etc...
#include <string.h> // strncat(), strcmp(), strncpy(), strlen(), strtok(), etc...
#include <dirent.h> // To use the DIR type. Includes: closedir(), opendir(), readdir(), etc...
#include <unistd.h> // PID, Signals, and Process Management
#include <sys/stat.h> // chmod(), mkdir(), mkfifo(), stat(), etc...
#include <time.h> // asctime(), clock(), time(), etc...
#include <assert.h> // assert(int expression)
#include <sys/wait.h> // used to wait for processes
#include <sys/types.h> // to work with file redirection
#include <fcntl.h> // to open files and redirect streams 
#include <signal.h> // To catch and react to signals

typedef struct comand command; // Can now use "command" structs without typing struct
struct comand
{ // Command container
	int  argc; // number of arguements
	char * argv[513]; // array of arguements
	int rDirIndex; // location of the file for redirection in argv array
	int rDir; // 0, 1, or -1 to indicate redirection no, l, and r
	int isBG; // 0 if not BG command, 1 if is BG command
};

typedef struct statUS state; // Can now use "stat" structs without typing struct
struct statUS 
{ // status of shell container
	int status; // exit val of programs
	pid_t bgPid[100]; // background pid list
	int processes; // number of background processes
};

void error(const char *, int); // errors and exits with int val
void signalHandler(int); // fun signal handler; says "ouch"
int doCd(command *); // parses args for cd and calls doCd
void prompt(command *);// prompts the user and puts it into argv format. returns int for argc
void getNN(char *, int); // get with No Newline
void freeComm(command *); // free the command list
void initComm(command *); // make mem for command list
void doExec(command *,state *); // Executes a command
void doExecL(command *,state *); // Executes a command with a redirection <
void doExecR(command *,state *); // Executes a command with a redirection >
void doExecBG(command *,state *); // Execute a background command
void checkRedir(command *); // sets rDir flags 0 < >, 0 -1 1 respectively
void printStatus(state); // print status as 1 or 0
void doComm(command *, state *); // execs command and stores status in state
void removeP(int[],int,int);
void waitBG(state *); // waits for bg processes
void checkBG(command * comm);

int main()
{
	// structs for commands and shell status //
	command comm; // command struct 
	comm.argc=0; // set num args to 0
	state stat; // shell status struct
	stat.processes=0; // set num bg processes to 0
	signal(SIGINT, signalHandler); // ignore ctr-c
	while(1) // loop forever
	{
		prompt(&comm); // get command from stdin
		if(strcmp(comm.argv[0], "\n") && (comm.argv[0][0] != '#'))
		{ // this is not a blank line, or a comment
			if(!strcmp(comm.argv[0], "exit")) return(0); // exit shell
			else if(!strcmp(comm.argv[0], "cd"))stat.status = doCd(&comm); // cd comm
			else if(!strcmp(comm.argv[0], "status"))printStatus(stat); // status comm
			else doComm(&comm, &stat); // perform alternate command
		}
		waitBG(&stat); // wait for any dead BG processes
	}
	return (0);
}

// Uses stored pid of background processes 
// to wait with nohang for dead background
// processes. The pids are stored in the 
// stat struct
void waitBG(state * stat)
{
	if(stat->processes>0)
	{ // if proccesses exist, wait for them
		int i;
		for(i=0;i<stat->processes;i++)
		{ // loop through bg processes	
			if(waitpid(stat->bgPid[i],&stat->status,WNOHANG))
			{ // if cPid has been waited on
				printf("BG Process ID %d Done\n",(int)stat->bgPid[i]);
	
				// Print exit status; Either exit stat or killed by sig // 
				if(WIFSIGNALED(stat->status))printf("Killed by signal: %d\n",WTERMSIG(stat->status));
				else printStatus(*stat);
				removeP(stat->bgPid,i,stat->processes); // remove cPid from array
				stat->processes-=1;	// dec number of bg processes
			}
		}
	}
}

void removeP(int array[],int i,int len)
{ // when a process is waited on, this removes it
	int j;
	for(j=i;j<(len-1);j++)
		array[j]=array[j+1]; // shift elements
}

void signalHandler(int signum)
{ // fun sig handler
	printf("\nOuch\n:");
	fflush(stdout);
}

void doComm(command * comm, state * stat)
{ // pick which type of command to execute
	if(!comm->rDir && !comm->isBG)doExec(comm,stat); // search for comm & exec if exists
	else if(comm->rDir==1)doExecR(comm,stat); // output to file
	else if(comm->rDir==-1)doExecL(comm,stat); // input from file
	else if(comm->isBG==1)doExecBG(comm,stat); // run as background process
}

void printStatus(state stat)
{ // prints the exit status of the previous command
	if(stat.status) printf("Exit Status: %d\n",1);
	else printf("Exit Status: %d\n",0);
}
	
void error(const char * erStr,int exVal)
{ // error message and exit
	perror(erStr);
	exit(exVal);
}

void checkRedir(command * comm)
{ // check if a command will require redirection
	int i = 0;
	comm->rDir = 0;
	for(i=0; i<comm->argc; i++)
	{ // check all elements for < or >
		if(!(strcmp(comm->argv[i], "<")) && ((i+1) <= comm->argc) )
		{
			comm->rDirIndex = i; // save spot for later use
			comm->rDir = -1; // set rDir flag
		}
		if(!(strcmp(comm->argv[i], ">")) && ((i+1) <= comm->argc) )
		{
			comm->rDirIndex = i; // save spot for later use
			comm->rDir = 1; // set rDir Flag
		}
	}
}

void prompt(command * comm)
{ // promt the user for a command, and return a formatted command
	
	freeComm(comm); // free argv and set argc = 0
	char str [513], * arg; // 513 because it will need to account for the newline char
	printf(": "); // Prompt
	getNN(str, 512); // get with No Newline
	arg = strtok(str, " "); // get the first arg, and set up to loop over str
	comm->argc = 0; // set argc
	while(arg!=NULL)
	{ // put args in argv
		comm->argv[comm->argc] = malloc(sizeof(strlen(arg))); // allocate mem for arg
   		strcpy(comm->argv[comm->argc], arg); // insert arg into array
		comm->argc = comm->argc + 1; // increment arg count
		arg=strtok(NULL, " "); // get token
	}
	comm->argv[comm->argc] = NULL; // NULL term argv
	checkRedir(comm); // set redirection flags as needed
	checkBG(comm);
}	

void checkBG(command * comm)
{ // set the isBG flag if needed 
	if(!strcmp(comm->argv[comm->argc-1], "&"))
	{ // This is be a background process	
		comm->isBG = 1;

		// Set & to NULL for exec purposes //
		free(comm->argv[comm->argc-1]);	
		comm->argv[comm->argc-1] = NULL;
	}
	else comm->isBG = 0; // not a bg process
}

void doExecBG(command * comm, state * stat)
{
	fflush(stdout); // clear stdout buffer before fork
	pid_t cPid = fork();
	switch(cPid)
	{
	case 0:;// I am the child
				
			// Set output to dev/null //
			int devNull = open("/dev/null",O_WRONLY); // Open dev/null
			dup2(devNull, 1); // Set stdout to NULL 
			dup2(devNull, 2); // Set stderr to NULL 
			execvp(comm->argv[0], comm->argv); // Execute command

			// If code below runs, exec failed //
			printf("ERROR: \"%s\" Command Not Found\n", comm->argv[0]);
			stat->status=1; // set fail status
			exit(1); // exit err
	case -1: // Fork ERROR
			error("ERROR fork",1);
	default: // I am the parent of a background child
				printf("Background Process ID: %d\n", (int)(cPid));
				stat->processes+=1; // increment the number of BG processes running
				stat->bgPid[stat->processes-1] = cPid; // ad cPid to bgP list
	}
}
	
void doExec(command * comm, state * stat)
{ // execute a normal command
	fflush(stdout); // clear stdout buffer before fork
	pid_t cPid = fork();
	switch(cPid)
	{
	case 0: // I am a child process
			signal(SIGKILL, SIG_DFL);
			execvp(comm->argv[0], comm->argv); // Execute command
			// If code below runs, exec failed //
			printf("ERROR: \"%s\" Command Not Found\n", comm->argv[0]);
			exit(1); // exit err
	case -1: // Fork error
			error("ERROR fork",1);
	default: // I am the parent process
			waitpid(cPid,&stat->status,0); // wait for child and store exit status
	}
}
	
void doExecR(command * comm,state * stat)
{ // This will redirect output of a command to a file
	fflush(stdout); // prevent the child from double writing the buffer to stdout
	pid_t cPid = fork(); // split child and parrent
	switch(cPid)
	{
	case 0: // I am the child process
			comm->argv[comm->rDirIndex] = NULL; // set redir symbol to NULL so exec stops here
			signal(SIGINT,SIG_DFL); // set program to be killable
 			// Open the requested file for output and make if it doesn't exist //
			int fd = open(comm->argv[comm->rDirIndex + 1], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
			dup2(fd, 1); // Set stdout to file
			dup2(fd, 2); // Set stderr to file
			execvp(comm->argv[0], comm->argv); // Execute command
			exit(1); // exit err
			break;
	case -1: // Fork ERROR
			error("ERROR fork",1);
	default: // I am the parent process
			waitpid(cPid,&stat->status,0); // wait for child and store exit status
	}
}
	
void doExecL(command * comm,state * stat)
{ // usees file for stdin on command
	FILE * fp = fopen(comm->argv[comm->rDirIndex+1],"r");
	if(fp)fclose(fp); // check that the file exists
	else
	{
		printf("ERROR: Cannot Open \"%s\" for Input\n", comm->argv[comm->rDirIndex+1]);
		stat->status = 1; // Set error status
		return;
	}
	fflush(stdout);
	pid_t cPid = fork();
	switch(cPid)
	{
	case 0: // I am a child process
			signal(SIGINT, SIG_DFL);
			freopen(comm->argv[comm->rDirIndex+1],"r",stdin); // set file as input
			free(comm->argv[comm->rDirIndex]);
			comm->argv[comm->rDirIndex] = NULL;
			execvp(comm->argv[0], comm->argv); // Execute command
			// If code below runs, exec failed //
			printf("ERROR: \"%s\" Command Not Found\n", comm->argv[0]);
			exit(1); // exit err
	case -1: // Fork error
			error("ERROR fork",1);
	default: // I am the parent process
			waitpid(cPid,&stat->status,0); // wait for child and store exit status
	}
}	

void freeComm(command * comm)
{ // free mem used by dynamic argv 
	while(comm->argc>0)
	{ // start at last argument and free all until argc = 0
		if(comm->argv[comm->argc])free(comm->argv[comm->argc]);
		comm->argc-=1;
	}
	int i = 0;
	for(i=0; i<512; i++)comm->argv[i] = NULL; // sett argv all to NULL
}
	

void getNN(char * str, int num)
{ // Strips newline from fgets input string
	if(!fgets(str, num, stdin)) // get input
		exit(0);
	if(str[strlen(str)-1]=='\n' && strlen(str)>1)str[strlen(str)-1]='\0'; // replace \n with NULL
}

int doCd(command * comm)
{ // change current working directory command
	if(comm->argc>1)
	{ // The directory was specified, so attempt CD
		DIR * dir = opendir(comm->argv[1]); // Try to open dir
		if(dir)
		{ // Directory exists, and is opened
   		 	closedir(dir); // No need to keep it open
			chdir(comm->argv[1]); // The directory is valid, so change CWD
			return(0);
		}
		else
		{ // Directory doesn't exist
			printf("Directory not found\n");	
			return(1);
		}
	}
	else
	{ // The directory was not specified, so instead CD to home directory
		DIR * dir = opendir(getenv("HOME")); // Open home dir
   	 	closedir(dir); // No need to keep it open
		chdir(getenv("HOME")); // The directory is valid, so change CWD
		return(0);
	}
}
