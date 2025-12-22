#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char** argv) {
  if(argc == 1) {
    printf("Call successful\n");
    exit(0);
  } else {
    void *pointer = malloc(5000000001);
    printf("Succesfully allocated\n");
    free(pointer);
    exit(0);
  }
}
