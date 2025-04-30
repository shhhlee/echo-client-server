#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#ifdef WIN32
#include <ws2tcpip.h>
#endif
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>

#ifdef WIN32
void myerror(const char* msg) { fprintf(stderr, "%s %lu\n", msg, GetLastError()); }
#else
#include <errno.h>
void myerror(const char* msg) { fprintf(stderr, "%s %s %d\n", msg, strerror(errno), errno); }
#endif

void usage() {
    printf("syntax: ts <port> [-e[-b]]\n");
    printf("sample: ts 1234 -e -b\n");
}

struct Param {
    uint16_t port{0};
    bool echo{false};
    bool broadcast{false};

    bool parse(int argc, char* argv[]) {
        if (argc < 2) return false;
        port = atoi(argv[1]);
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-e") == 0) echo = true;
            if (strcmp(argv[i], "-b") == 0) broadcast = true;
        }
        return port != 0;
    }
} param;

std::vector<int> clientSds;
std::mutex mtx;

void broadcastMessage(const char* buf, size_t len) {
    std::lock_guard<std::mutex> lock(mtx);
    for (int sd : clientSds) {
        ::send(sd, buf, len, 0);
    }
}

void recvThread(int sd) {
    static const int BUFSIZE = 65536;
    char buf[BUFSIZE];
    printf("connected: %d\n", sd);

    while (true) {
        ssize_t len = ::recv(sd, buf, BUFSIZE - 1, 0);
        if (len <= 0) {
            fprintf(stderr, "recv return %zd\n", len);
            myerror("recv");
            break;
        }

        buf[len] = '\0';
        printf("recv from %d: %s", sd, buf);
        fflush(stdout);

        if (param.echo) {
            std::string prefix = "[echo] ";
            std::string msg = prefix + std::string(buf, len);
            ::send(sd, msg.c_str(), msg.length(), 0);
        }

        if (param.broadcast) {
            std::string prefix = "[broadcast] ";
            std::string msg = prefix + std::string(buf, len);
            broadcastMessage(msg.c_str(), msg.length());
        }


    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        clientSds.erase(std::remove(clientSds.begin(), clientSds.end(), sd), clientSds.end());
    }

    ::close(sd);
    printf("disconnected: %d\n", sd);
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

    int sd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        myerror("socket");
        return -1;
    }

#ifdef __linux__
    {
        int optval = 1;
        if (::setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
            myerror("setsockopt");
            return -1;
        }
    }
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(param.port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(sd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        myerror("bind");
        return -1;
    }

    if (::listen(sd, 5) == -1) {
        myerror("listen");
        return -1;
    }

    printf("listening on port %d\n", param.port);

    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int newsd = ::accept(sd, (struct sockaddr*)&clientAddr, &clientLen);
        if (newsd == -1) {
            myerror("accept");
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            clientSds.push_back(newsd);
        }

        std::thread t(recvThread, newsd);
        t.detach();
    }

    ::close(sd);
}
