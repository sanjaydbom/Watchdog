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
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

int pid = -1;

static void handler(int sig) {
    kill(pid, sig);
}

int main(int argc, char **argv) {
    struct sigaction sa;
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    const char *stdout_label = " [INFO]   ";
    const char *stderr_label = " [ERROR]  ";

    const int numTries = 10;
    char* successfulChild[] = {"./ChildProcess", NULL}; //This call will succeed, return 0
    char* failingChild[] = {"./ChildProcess", "1", NULL}; //This call will fail, return -1
    char*const * args;

    time_t now;
    struct tm* local_time;
    if(argc == 1) {
        args = successfulChild;
    } else {
        args = failingChild;
    }

    FILE* logFile = fopen("./log.txt", "w");
    int waitTimeMS = 1;
    for (int i = 0; i < numTries; i++) {
        //exponential backoff
        sleep(waitTimeMS / 1000);
        waitTimeMS *= 3;
        printf("Attempt %i at running process\n", i+1);

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

        //setup to make reading event driven
        //The parent process will wake up when there is something to read in the pipe and sleep at all other times
        //This significantly decreases CPU usage(Went from using most of CPU before to using 0.01% of CPU)

        struct kevent event[2];
        struct kevent tevent;
        int kq, fd, ret;

        kq = kqueue(); //sets up Kqueue

        //Adds an event to kqueue when there is something to read in a pipe. The label corresponds to the pipe.
        EV_SET(&event[0], pipe_fd[0][0], EVFILT_READ, EV_ADD, 0, 0, (void *)stdout_label);
        EV_SET(&event[1], pipe_fd[1][0], EVFILT_READ, EV_ADD, 0, 0, (void *)stderr_label);
        kevent(kq, &event, 2, NULL, 0, NULL);

        //create child process
        pid = fork();
        if(pid == -1) {
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

            //run the argument
            execv(args[0], args);
        } else { //Parent Process
            //close write end of the pipe
            close(pipe_fd[0][1]);
            close(pipe_fd[1][1]);
            int numClosed = 0;

            //Wait until we have closed both pipes
            while (numClosed < 2) {
                //This line will set the parent process to sleep until there is an event. The event is stored in tevent.
                ret = kevent(kq,	NULL, 0, &tevent, 1, NULL);
                if (ret > 0) {
                    int source_fd = (int)tevent.ident;//which port the event came from
                    char output_buffer[2048];
                    char time_buffer[100];
                    int numRead = read(source_fd, output_buffer, 2048);

                    if (numRead == 0) { //Close file and increment counter
                      close(source_fd);
                      numClosed++;
                    } else { //Add data to log with time
                        now = time(NULL);
                        local_time = localtime(&now);
                        strftime(time_buffer, sizeof(time_buffer), "***%m-%d-%Y %H:%M:%S***", local_time);

                        fwrite(time_buffer, sizeof(char), strlen(time_buffer), logFile);
                        fwrite((char *) tevent.udata, sizeof(char), strlen((char *) tevent.udata), logFile);
                        fwrite(output_buffer, sizeof(char), sizeof(char) * numRead, logFile);
                    }
                }
            }

            //wait for child process to finish and see if it succeeded
            int status;
            waitpid(pid, &status, 0);
            if (status == 0) {
                printf("Child exited successfully\n");
                exit(0);
            }
        }
    }

    //If no child process succeeds after numTry attempts, fail and return -1
    printf("Initilzing child failed. Exiting...\n");
    exit(-1);
}
