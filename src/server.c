#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define BASE_IMPLEMENTATION
#include "base.h"

#define PORT 8080
#define BACKLOG 64

#define STR_FMT "%.*s"
#define STR_ARG(s) (int)(s).length, (s).data

//TODO: Switch to LogX functions from base.h

int main(int argc, const char *argv[]) {
  struct sockaddr_in serverAddress;
  serverAddress.sin_family = AF_INET; // IPv4
  serverAddress.sin_port = htons(PORT);
  serverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost

  int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  int reuseAddress = 1;
  // Stop the OS from holding onto the packet after restarts
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuseAddress,
             sizeof(int));

  if (bind(serverSocket, (struct sockaddr *)&serverAddress,
           sizeof(serverAddress)) < 0) {
    printf("Error: Can't bind the socket!\n");
    return 1;
  }

  if (listen(serverSocket, BACKLOG) < 0) {
    printf("Error: Can't listen on the socket!\n");
    return 1;
  }

  char hostBuffer[NI_MAXHOST];
  int error = getnameinfo((struct sockaddr *)&serverAddress,
                          sizeof(serverAddress), hostBuffer, sizeof(hostBuffer),
                          NULL, 0, 0);

  if (error != 0) {
    printf("Error: %s\n", gai_strerror(error));
    return 1;
  }

  printf("\nServer is listening on http://%s:%d/\n\n", hostBuffer,
         PORT);

  while (true) {
    struct sockaddr_in clientAddress;
    socklen_t clientAddressSize = sizeof(clientAddress);
    int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress,
                              &clientAddressSize);
    if (clientSocket < 0) {
      printf("Warning: Failed to accept a client!\n");
      continue;
    }

    // Allocate an arena for this request
    Arena *arena = ArenaCreate(4096);

    StringBuilder request = SBCreate(arena);

    char buffer[4096];

    while (true) {
      ssize_t n = recv(clientSocket, buffer, sizeof(buffer), 0);

      if (n <= 0) {
        break;
      }

      SBAdd(&request, (String){
                          .data = buffer,
                          .length = n,
                      });

      // Check if we received the end of HTTP headers.
      if (StrIncludes(request.buffer, S("\r\n\r\n"))) {
        break;
      }
    }

    printf("Request: "STR_FMT"\n", STR_ARG(request.buffer));
  }
}