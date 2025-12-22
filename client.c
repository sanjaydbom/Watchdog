#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <sys/event.h>
#include <unistd.h>
#define SOCKET_NAME "/tmp/parent.socket"

int main() {
    struct sockaddr_un my_addr;
    int connectionSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sun_family = AF_UNIX;
    strncpy(my_addr.sun_path, SOCKET_NAME, sizeof(my_addr.sun_path) - 1);
    if(connect(connectionSocket, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1) {
      perror("Connect Error!");
      exit(-1);
    }

    int kq = kqueue();
    struct kevent events[2];
    struct kevent tevent;

    EV_SET(&events[0], connectionSocket, EVFILT_READ, EV_ADD, 0, 0,
           (void *)"SERVER");
    EV_SET(&events[1], STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, (void *)"STDIN");
    kevent(kq, &events, 2, NULL, 0, NULL);
    while (1) {
      int ret = kevent(kq, NULL, 0, &tevent, 1, NULL);
      if (ret > 0) {
          if (tevent.ident == STDIN_FILENO){
              char buffer[128];
              int num = read(STDIN_FILENO, buffer, 127);
              buffer[num] = 0;
              write(connectionSocket, buffer, num);
              EV_SET(&tevent, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, (void *)"STDIN");
              kevent(kq, &tevent, 1, NULL, 0, NULL);
          } else {
              char buffer[128];
              int num = read(connectionSocket, buffer, 127);
              buffer[num] = 0;
              printf("RESPONSE: %s\n", buffer);
              fflush(stdout);
              if (num == 0)
                break;
          }
      }
    }
    exit(0);
}
