#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>

int main(int argc, char** argv) {
  const int numTries = 10;
  char* successfulChild[] = {"./ChildProcess", NULL};
  char* failingChild[] = {"./ChildProcess", "1", NULL};
  char*const * args;

  if(argc == 1) {
    args = successfulChild;
  } else {
    args = failingChild;
  }

  for(int i = 0; i < numTries; i++) {
    printf("Attempt %i at running process\n", i+1);
    int pid = fork();
    if(pid == -1) {
      return -1;
    }
    if(pid == 0) {
      execv(args[0], args);
    }
    else {
     int status;
     waitpid(pid, &status, 0);
     if(status == 0)
       exit(0);
    }
  }
  printf("Initilzing child failed. Exiting...\n");
  exit(-1);
}
