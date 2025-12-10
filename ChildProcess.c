#include <stdlib.h>
#include <stdio.h>
int main(int argc, char** argv) {
  if(argc == 1) {
    printf("Call successful\n");
    exit(0);
  } else {
    fprintf(stderr, "Call failed\n");
    exit(-1);
  }
}
