/*
 * Creates a child process running the program ChildProcess.
 * If the program gets no args, then it runs a version of the child process that will succeed
 * Otherwise, it will run the failing child process
 * It tries to run the child process at most numTries times and if the child succeeds,
 * code will exit successfully
 * All outputs of the child process will be send to the parent and then placed in a log file
 *
*/
#define _GNU_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1
#define _XPLATFORM_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <limits.h>
#include <math.h>
#include <errno.h>

#define SOCKET_NAME "/tmp/parent.socket" //socket that client must connect to

#define LOG_FILE "./log.txt" //name of the file where all the data will be stored

//labels for each pipe for the kqueue
#define stdout_label " [INFO]   "
#define stderr_label " [ERROR]  "
#define server_label "SERVER"
#define client_label "CLIENT"
#define timer_label  "TIMER"
#define pass ;

//pid of the child process
int pid = -1;

static void handler(int sig) { kill(pid, sig); }
//handles new client connections
void handleSocketCall(int kq, struct kevent tevent);
//handles data in a established client socket
int handleClientCall(struct kevent tevent, int *idle, int *i);
//sets up socket for clients to connect to
int SetUpSocket();
//sets up all the signals that the parent process will handle
void CreateSignalHandling();
//gets the current time formated to be used for the log file
void getTime(char *time_buffer);
//Sets memory limit in megabytes for the child process, but doesn't work on apple
void setMemoryLimit(int limitMB);
//reads data from child process
void readChildData(struct kevent tevent, FILE *logFile, int *numClosed);

int main(int argc, char **argv) {
    //signal handling to kill child
    CreateSignalHandling();
    //set up connection socket for communication with user
    int connectionSocket = SetUpSocket();

    //decides if the child should pass or fail
    //This is only used for testing, delete this for the actual code
    const int numTries = 10;
    char* successfulChild[] = {"./ChildProcess", NULL}; //This call will succeed, return 0
    char* failingChild[] = {"./ChildProcess", "1", NULL}; //This call will fail, return -1
    char** args;
    if(argc == 1) {
        args = successfulChild;
    } else {
        args = failingChild;
    }

    FILE *logFile = fopen(LOG_FILE, "w"); //Log file


    //setup to make reading event driven
    //The parent process will wake up when there is something to read in the pipe and sleep at all other times
    //This significantly decreases CPU usage(Went from using most of CPU before to using 0.01% of CPU)
    int kq = kqueue(); //sets up kqueue
    struct kevent server_socket_event, tevent;
    //sets up event when there is data in the main connection socket
    EV_SET(&server_socket_event, connectionSocket, EVFILT_READ, EV_ADD, 0, 0, (void *)server_label);
    kevent(kq, &server_socket_event, 1, NULL, 0, NULL);

    int waitTimeMS = 1;
    int idle = 0;//flag for if the program is idle or not
    printf("Starting child processes\n");

    for (int i = 0; i < numTries; i++) {
        //exponential backoff/
        int restart = 0;
        struct kevent timer_event;
        while (1) {
          if (idle == 0) {
              //timer only starts if we aren't idle
                int timer_wait = (int)(waitTimeMS * pow(3.0, i) / 1000);
                EV_SET(&timer_event, 0, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_SECONDS, timer_wait, (void*)timer_label);
                kevent(kq, &timer_event, 1, NULL, 0, NULL);
            }
            int ret = kevent(kq, NULL, 0, &tevent, 1, NULL);

            if (strcmp(tevent.udata, timer_label) == 0 && idle == 0) {
                break;
            } else if (strcmp(tevent.udata, server_label) == 0) {
                handleSocketCall(kq, tevent);
            } else if (strcmp(tevent.udata, client_label) == 0) {
              restart = handleClientCall(tevent, &idle, &i);
            }
        }
        //if we restart we restart the loop
        if(restart)
            continue;
        printf("Attempt %i at running process\n", i + 1);

        //create pipe to send output from child process to parent process
        //One for standard output and one for standard error
        int pipe_fd[2][2];
        if(pipe(pipe_fd[0]) < 0)
            exit(-1);
        if(pipe(pipe_fd[1]) < 0)
            exit(-1);
        //Makes the read non-blocking on both pipes(I dont think we need this but I'll test it later)
        fcntl(pipe_fd[0][0], F_SETFL, O_NONBLOCK);
        fcntl(pipe_fd[1][0], F_SETFL, O_NONBLOCK);

        //events for when there is data at the child STDOUT and STDERR
        struct kevent event[2];
        //Adds an event to kqueue when there is something to read in a pipe. The label corresponds to the pipe.
        EV_SET(&event[0], pipe_fd[0][0], EVFILT_READ, EV_ADD, 0, 0, (void *)stdout_label);
        EV_SET(&event[1], pipe_fd[1][0], EVFILT_READ, EV_ADD, 0, 0, (void *)stderr_label);
        kevent(kq, &event, 2, NULL, 0, NULL);

        //create child process
        pid = fork();
        if (pid == -1) {
            perror("Fork Failed\n");
            exit(-1);
        }

        if(pid == 0) {
            //Send all outputs through pipe to parent process
            dup2(pipe_fd[0][1], 1);
            dup2(pipe_fd[1][1], 2);

            //important to close the pipe read ends to make sure the pipe can terminate properly
            close(pipe_fd[0][0]);
            close(pipe_fd[1][0]);
            close(pipe_fd[0][1]);
            close(pipe_fd[1][1]);

            //limits
            setMemoryLimit(50 * 1024 * 1024);

            //run the argument
            execv(args[0], args);
            perror("execv");
            exit(-1);
        } else { //Parent Process
            //close write end of the pipe
            close(pipe_fd[0][1]);
            close(pipe_fd[1][1]);
            int numClosed = 0;

            //Wait until we have closed both pipes
            while (numClosed < 2) {
                //This line will set the parent process to sleep until there is an event. The event is stored in tevent.
                int ret = kevent(kq, NULL, 0, &tevent, 1, NULL);
                if (ret > 0) {
                    tevent.udata = (char*) tevent.udata;
                    if (strcmp(tevent.udata, server_label) == 0) {
                        handleSocketCall(kq, tevent);
                    } else if(strcmp(tevent.udata, client_label) == 0) {
                        handleClientCall(tevent, &idle, &i);
                    } else if (strcmp(tevent.udata, timer_label) == 0) {
                        pass;
                    } else {
                       readChildData(tevent, logFile, & numClosed);
                    }
                }
            }

            //wait for child process to finish and see if it succeeded
            int status;
            waitpid(pid, &status, 0);
            if (status == 0) {
                close(connectionSocket);
                printf("Child exited successfully\n");
                exit(0);
            }
            pid = -1;
        }
    }

    //If no child process succeeds after numTry attempts, fail and return -1
    printf("Initilzing child failed. Exiting...\n");
    close(connectionSocket);
    exit(-1);
}


void handleSocketCall(int kq, struct kevent tevent) {
  printf("CLIENT CONNECTED\n");
  int tempSocket = accept((int)tevent.ident, NULL, NULL);
  struct kevent event;
  EV_SET(&event, tempSocket, EVFILT_READ, EV_ADD, 0, 0, (void *)client_label);
  kevent(kq, &event, 1, NULL, 0, NULL);
}

int handleClientCall(struct kevent tevent, int* idle, int* i) {
    char buffer[32];
    char response[100];
    int num = read((int)tevent.ident, buffer, sizeof(buffer)-1);
    buffer[num] = 0;
    if (num == 0) {
      close(tevent.ident);
      return 0;
    }
    if (strncmp(buffer, "GET_STATUS\n", num) == 0) {
        if(*idle)
          sprintf(response, "IDLE\n");
        else
            sprintf(response, "RUNNING | PID %d\n", pid);
        write((int)tevent.ident, response, sizeof(response));
        return 0;
    } else if (strncmp(buffer, "RESTART\n", num) == 0) {
        if (pid != -1)
            kill(pid, SIGTERM);
        *i = -1;
        *idle = 0;
        return 1;
    } else if (strncmp(buffer, "STOP\n", num) == 0) {
        *idle = 1;
        if (pid != -1) {
            i--;
            kill(pid, SIGTERM);
        }
        return 0;
    } else if (strncmp(buffer, "RESUME\n", num) == 0) {
        *idle = 0;
        return 0;
    } else {
        sprintf(response, "INVALID COMMAND\n");
        write((int)tevent.ident, response, sizeof(response));
        return 0;
    }
}

int SetUpSocket() {
    unlink(SOCKET_NAME);
    struct sockaddr_un  my_addr;
    int connectionSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sun_family = AF_UNIX;
    strncpy(my_addr.sun_path, SOCKET_NAME, sizeof(my_addr.sun_path) - 1);

    //printf("%i\n", connectionSocket);
    if (bind(connectionSocket, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1) {
        perror("Bind Error\n");
        exit(-1);
    }
    if (listen(connectionSocket, 10) == -1) {
        perror("Listen Error\n");
        exit(-1);
    }

    return connectionSocket;
}

void CreateSignalHandling() {
    struct sigaction sa;
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
}

void getTime(char* time_buffer) {
    time_t now;
    struct tm* local_time;

    now = time(NULL);
    local_time = localtime(&now);
    strftime(time_buffer, sizeof(time_buffer), "***%m-%d-%Y %H:%M:%S***", local_time);
}

void setMemoryLimit(int limit) {
    //limits
    #ifndef __APPLE__
    struct rlimit memoryLimit;
    if (getrlimit(RLIMIT_RSS, &memoryLimit) == -1) {
        perror("Get limit failure");
        int err = errno;
        printf("%i\n", err);
        exit(-1);
    }
    printf("Current soft limit: %llu, hard limit: %llu\n",
            (unsigned long long)memoryLimit.rlim_cur, (unsigned long long)memoryLimit.rlim_max);
    memoryLimit.rlim_cur = limit;
    if (setrlimit(RLIMIT_RSS, &memoryLimit) == -1) {
        perror("Set limit failure");
        int err = errno;
        printf("%i\n", err);
        exit(-1);
    }
    #endif

}

void readChildData(struct kevent tevent, FILE *logFile, int* numClosed) {
    int source_fd = (int)tevent.ident;//which port the event came from
    char output_buffer[2048];
    char time_buffer[100];
    int numRead = read(source_fd, output_buffer, 2047);

    if (numRead == 0) { //Close file and increment counter
        close(source_fd);
        (*numClosed)++;
    } else { //Add data to log with time
        getTime(time_buffer);
        fwrite(time_buffer, sizeof(char), strlen(time_buffer), logFile);
        fwrite((char *) tevent.udata, sizeof(char), strlen((char *) tevent.udata), logFile);
        fwrite(output_buffer, sizeof(char), sizeof(char) * numRead, logFile);
    }
}
