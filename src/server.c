#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define BASE_IMPLEMENTATION
#include "base.h"
#include "base_ext.h"

#define PORT 8080
#define BACKLOG 64

#define HTDOCS S("htdocs")

#define HTTP_200 "200 OK"
#define HTTP_404 "404 Not Found"
#define HTTP_500 "500 Internal Server Error"

// Source - https://stackoverflow.com/a/42561141
// Posted by FogleBird
// Retrieved 2026-09-07, License - CC BY-SA 3.0

int sendAll(int sd, char *data, int length) {
  int count = 0;
  while (count < length) {
    int n = send(sd, data + count, length, 0);
    if (n == -1) {
      return -1;
    }
    count += n;
    length -= n;
  }
  return 0;
}

int main(int argc, const char *argv[]) {
  LogInit();

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
    LogError("Can't bind the socket! (Is another server running?)");
    return 1;
  }

  if (listen(serverSocket, BACKLOG) < 0) {
    LogError("Can't listen on the socket!");
    return 1;
  }

  char hostBuffer[NI_MAXHOST];
  int error =
      getnameinfo((struct sockaddr *)&serverAddress, sizeof(serverAddress),
                  hostBuffer, sizeof(hostBuffer), NULL, 0, 0);

  if (error != 0) {
    LogError("%s", gai_strerror(error));
    return 1;
  }

  LogInfo("Server is listening on http://%s:%d/", hostBuffer, PORT);

  while (true) {
    struct sockaddr_in clientAddress;
    socklen_t clientAddressSize = sizeof(clientAddress);
    int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress,
                              &clientAddressSize);
    if (clientSocket < 0) {
      LogWarn("Failed to accept a client!");
      continue;
    }

    // Disable https://en.wikipedia.org/wiki/Nagle's_algorithm
    int noDelay = 1;
    setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &noDelay,
               sizeof(noDelay));

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

    LogDebug("Request: " STR_FMT "", STR_ARG(request.buffer));

    size_t lineIndex = 0;
    SplitView lines = StrSplitView(request.buffer, S("\r\n"));
    while (true) {
      String line = SplitViewNext(&lines);
      if (StrIsEmpty(line))
        break;

      LogDebug("Line %zu: " STR_FMT, lineIndex, STR_ARG(line));

      if (lineIndex == 0) {
        // Request-Line
        SplitView parts = StrSplitView(line, S(" "));
        String method = SplitViewNext(&parts);
        String uri = SplitViewNext(&parts);
        String version = SplitViewNext(&parts);

        if (!StrEq(method, S("GET"))) {
          break;
        }

        // Valid request, handle
        char *resCode = HTTP_200;

        if (StrEq(uri, S("/"))) {
          uri = S("/index.html");
        }

        // Read the requested file
        String filePath = PathJoin(arena, HTDOCS, uri);

        LogDebug("Reading file: " STR_FMT, STR_ARG(filePath));
        FileReadResult file = FileReadEz(arena, filePath);
        if (file.error != SUCCESS) {
          if (file.error == FILE_NOT_FOUND) {
            resCode = HTTP_404;
          } else {
            resCode = HTTP_500;
            String err = ErrToStr(file.error);
            LogError("Failed to get file stats: " STR_FMT, STR_ARG(err));
          }
        }

        // Construct the response
        StringBuilder response = SBCreate(arena);

        SBAddF(&response, "HTTP/1.1 %s \r\n", resCode);
        SBAdd(&response, S("Content-Length: "));
        SBAddF(&response, "%ul", (uint64_t)file.data.length);
        SBAdd(&response, S("\r\n"));
        SBAdd(&response, S("Content-Type: text/html\r\n"));
        SBAdd(&response, S("Connection: close\r\n"));
        SBAdd(&response, S("\r\n"));
        SBAdd(&response, file.data);

        LogDebug("Response: " STR_FMT, STR_ARG(response.buffer));

        sendAll(clientSocket, response.buffer.data, response.buffer.length);
      }

      ++lineIndex;
    }
  }
}