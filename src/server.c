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
#include "cgi.h"

#define BACKLOG 64

#define LDOCS S("dist")

typedef struct {
  String method;
  String uri;
  String version;
} HttpRequest;

typedef struct {
  String resCode;
  String body;
} HttpResponse;

const String HTTP_200 = S("200 OK");
const String HTTP_400 = S("400 Bad Request");
const String HTTP_404 = S("404 Not Found");
const String HTTP_405 = S("405 Method Not Allowed");
const String HTTP_500 = S("500 Internal Server Error");

bool sendAll(int sd, const char *data, size_t length) {
  size_t sent = 0;
  while (sent < length) {
    ssize_t n = send(sd, data + sent, length - sent, 0);

    if (n <= 0)
      return false;

    sent += n;
  }
  return true;
}

String getErrorPage(Arena *arena, String resCode) {
  StringBuilder builder = SBCreate(arena);
  SBAddF(&builder, "<center><h1>%S</h1></center>", resCode);
  return builder.buffer;
}

bool parseRequest(String reqData, HttpRequest *request) {
  size_t lineIndex = 0;
  SplitView lines = StrSplitView(reqData, S("\r\n"));
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

      *request =
          (HttpRequest){.method = method, .uri = uri, .version = version};
      return true;
    }

    ++lineIndex;
  }

  return false;
}

void sendResponse(Arena *arena, int clientSocket, HttpResponse response) {
  StringBuilder builder = SBCreate(arena);

  SBAddF(&builder, "HTTP/1.1 %S\r\n", response.resCode);
  SBAdd(&builder, S("Content-Length: "));
  SBAddF(&builder, "%ul", (uint64_t)response.body.length);
  SBAdd(&builder, S("\r\n"));
  SBAdd(&builder, S("Content-Type: text/html\r\n"));
  SBAdd(&builder, S("Connection: close\r\n"));
  SBAdd(&builder, S("\r\n"));
  SBAdd(&builder, response.body);

  LogDebug("Response: " STR_FMT, STR_ARG(builder.buffer));

  sendAll(clientSocket, builder.buffer.data, builder.buffer.length);
}

HttpResponse serveFile(Arena *arena, CGI *cgi, String uri) {
  HttpResponse resp = {.resCode = HTTP_200};

  if (StrEq(uri, S("/"))) {
    uri = S("/index.lua");
  }

  // Read the requested file
  String filePath = PathJoin(arena, LDOCS, uri);

  LogDebug("Reading file: " STR_FMT, STR_ARG(filePath));
  FileStatsResult stats = FileStats(filePath);
  if (stats.error == SUCCESS) {
    String output;
    // This assumes that filePath is always null terminated,
    // which is correct in our case
    if (cgi_run(cgi, arena, filePath.data, &output)) {
      resp.body = output;
    } else {
      resp.resCode = HTTP_500;
      resp.body = getErrorPage(arena, HTTP_500);
      LogError("Failed to run Lua route '" STR_FMT "': " STR_FMT, STR_ARG(uri),
               STR_ARG(output));
    }
  } else {
    if (stats.error == FILE_NOT_FOUND) {
      resp.resCode = HTTP_404;
    } else {
      resp.resCode = HTTP_500;
      String err = ErrToStr(stats.error);
      LogError("Failed to check file: " STR_FMT, STR_ARG(err));
    }

    resp.body = getErrorPage(arena, resp.resCode);
  }

  return resp;
}

HttpResponse errorResponse(Arena *arena, String resCode) {
  return (HttpResponse){.resCode = resCode,
                        .body = getErrorPage(arena, resCode)};
}

void handleRequest(Arena *arena, int clientSocket, CGI *cgi, String reqData) {
  HttpRequest request;
  HttpResponse response;

  if (!parseRequest(reqData, &request))
    response = errorResponse(arena, HTTP_400);
  else if (!StrEq(request.method, S("GET")))
    response = errorResponse(arena, HTTP_405);
  else
    response = serveFile(arena, cgi, request.uri);

  sendResponse(arena, clientSocket, response);
}

int main(int argc, const char *argv[]) {
  LogInit();

  int port = 8080;
  if (argc > 1) {
    port = atoi(argv[1]);
    if (port == 0) {
      LogError("Please pass a valid port argument.");
      return 1;
    }
  }

  CGI *cgi = cgi_init();

  struct sockaddr_in serverAddress;
  serverAddress.sin_family = AF_INET; // IPv4
  serverAddress.sin_port = htons(port);
  serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

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

  LogInfo("Server is listening on http://%s:%d/", hostBuffer, port);

  while (true) {
    struct sockaddr_in clientAddress;
    socklen_t clientAddressSize = sizeof(clientAddress);
    int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress,
                              &clientAddressSize);
    if (clientSocket < 0) {
      LogWarn("Failed to accept a client!");
      continue;
    }

    // Allocate an arena for this request
    Arena *arena = ArenaCreate(4096);

    StringBuilder request = SBCreate(arena);

    char recvBuf[4096];
    while (true) {
      ssize_t n = recv(clientSocket, recvBuf, sizeof(recvBuf), 0);

      if (n <= 0) {
        break;
      }

      SBAdd(&request, (String){
                          .data = recvBuf,
                          .length = n,
                      });

      // Check if we received the end of HTTP headers.
      if (StrIncludes(request.buffer, S("\r\n\r\n"))) {
        break;
      }
    }

    LogDebug("Request: " STR_FMT, STR_ARG(request.buffer));

    handleRequest(arena, clientSocket, cgi, request.buffer);

    close(clientSocket);
    ArenaFree(arena);
  }

  cgi_shutdown(cgi);
}