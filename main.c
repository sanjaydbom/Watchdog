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

    struct kevent event;
    struct kevent tevent;

    int kq, fd, ret;



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
    for(int i = 0; i < numTries; i++) {
        printf("Attempt %i at running process\n", i+1);

        //create pipe to send output from child process to parent process
        //One for standard output and one for standard error
        int pipe_fd[2][2];
        if(pipe(pipe_fd[0]) < 0)
            exit(-1);
        if(pipe(pipe_fd[1]) < 0)
            exit(-1);
        fcntl(pipe_fd[0][0], F_SETFL, O_NONBLOCK);
        fcntl(pipe_fd[1][0], F_SETFL, O_NONBLOCK);


        /*kq = kqueue();

        EV_SET(&event, pipe_fd[0][1], EVFILT_VNODE, EV_ADD, NOTE_WRITE, 0,
               NULL);
        ret = kevent(kq, &event, 1, NULL, 0,	NULL);*/


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
            int stdout = 1, stderr = 1;

            while(stdout + stderr != 0){
                //Catch output from standard output of child, format it, and add it to the log
                if(stdout != 0) {
                    char output_buffer[2048];
                    char time_buffer[100];
                    stdout = read(pipe_fd[0][0], output_buffer, 2048);

                    if (stdout == 0) {
                        close(pipe_fd[0][0]);
                    } else {
                        now = time(NULL);
                        local_time = localtime(&now);
                        strftime(time_buffer, sizeof(time_buffer),
                                 "***%m-%d-%Y %H:%M:%S*** [INFO]   ", local_time);

                        fwrite(time_buffer, sizeof(char), strlen(time_buffer), logFile);
                        fwrite(output_buffer, sizeof(char), sizeof(char) * stdout, logFile);
                    }
                }
                //Catch output from standard error of child, format it, and add it to the log
                if(stderr != 0) {
                    char output_buffer[2048];
                    char time_buffer[100];
          
                    stderr = read(pipe_fd[1][0], output_buffer, 2048);

                    if (stderr == 0) {
                        close(pipe_fd[1][0]);
                    } else {
                        now = time(NULL);
                        local_time = localtime(&now);
                        strftime(time_buffer, sizeof(time_buffer), "***%m-%d-%Y %H:%M:%S*** [ERROR]    "
                                 , local_time);

                        fwrite(time_buffer, sizeof(char), strlen(time_buffer), logFile);
                        fwrite(output_buffer, sizeof(char),
                               sizeof(char) * stderr, logFile);
                    }
                }
            }

            //wait for child process to finish and see if it succeeded
            int status;
            waitpid(pid, &status, 0);
            if (status == 0) {
                printf("Child exited successfully\n");
                exit(0);
            } else {
              usleep(waitTimeMS*1000);
              waitTimeMS *= 3;
            }
        }
    }

    //If no child process succeeds after numTry attempts, fail and return -1
    printf("Initilzing child failed. Exiting...\n");
    exit(-1);
}
