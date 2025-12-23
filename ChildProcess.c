#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char** argv) {
  if(argc == 1) {
    printf("Call successful\n");
    exit(0);
  } else {
      while(1);
      exit(0);
  }
}
