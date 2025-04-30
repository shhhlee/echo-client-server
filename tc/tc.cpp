#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#ifdef WIN32
#include <ws2tcpip.h>
#endif
#include <iostream>
#include <thread>

#ifdef WIN32
void myerror(const char* msg) {
    fprintf(stderr, "%s %lu\n", msg, GetLastError());
}
#else
#include <errno.h>
void myerror(const char* msg) {
    fprintf(stderr, "%s %s %d\n", msg, strerror(errno), errno);
}
#endif

void usage() {
    printf("syntax: tc <ip> <port>\n");
    printf("sample: tc 127.0.0.1 1234\n");
}

struct Param {
    char* ip{nullptr};
    char* port{nullptr};

    bool parse(int argc, char* argv[]) {
        if (argc != 3) return false;
        ip = argv[1];
        port = argv[2];
        return true;
    }
} param;

void recvThread(int sd) {
    const int BUFSIZE = 65536;
    char buf[BUFSIZE];
    while (true) {
        ssize_t res = recv(sd, buf, BUFSIZE - 1, 0);
        if (res <= 0) {
            fprintf(stderr, "recv returned %zd\n", res);
            myerror("recv");
            break;
        }
        buf[res] = '\0';
        printf("%s", buf);
        fflush(stdout);
    }
    close(sd);
    exit(0);
}

int main(int argc, char* argv[]) {
    if (!param.parse(argc, argv)) {
        usage();
        return -1;
    }

#ifdef WIN32
    WSAData wsaData;
    WSAStartup(0x0202, &wsaData);
#endif

    struct addrinfo aiInput, *aiOutput, *ai;
    memset(&aiInput, 0, sizeof(aiInput));
    aiInput.ai_family = AF_INET;
    aiInput.ai_socktype = SOCK_STREAM;

    int res = getaddrinfo(param.ip, param.port, &aiInput, &aiOutput);
    if (res != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
        return -1;
    }

    int sd;
    for (ai = aiOutput; ai != nullptr; ai = ai->ai_next) {
        sd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sd != -1) break;
    }

    if (ai == nullptr) {
        fprintf(stderr, "cannot create socket\n");
        return -1;
    }

    if (connect(sd, ai->ai_addr, ai->ai_addrlen) == -1) {
        myerror("connect");
        return -1;
    }

    printf("connected to %s:%s\n", param.ip, param.port);
    std::thread t(recvThread, sd);
    t.detach();

    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) break;

        line += "\n";
        ssize_t sent = send(sd, line.c_str(), line.length(), 0);
        if (sent <= 0) {
            fprintf(stderr, "send returned %zd\n", sent);
            myerror("send");
            break;
        }
    }

    close(sd);
}
