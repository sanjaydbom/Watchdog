#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char** argv) {
  if(argc == 1) {
    printf("Call successful\n");
    exit(0);
  } else {
    fprintf(stderr, "Call failed\n");
    sleep(10);
    exit(-1);
  }
}
