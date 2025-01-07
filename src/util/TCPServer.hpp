#pragma once

#include <cstring>
#include <atomic>
#include <thread>
#include <future>
#include <vector>
#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "../util/Log.hpp"

namespace cst {
class TCPServer {

    int mSocket;
    int mPort;
    std::atomic<bool> mRunning = false;
    std::thread mListenerThread;
    std::vector<std::future<void>> mFutures;

    void setNonBlocking(int socket) const {
        int flags = fcntl(socket, F_GETFL, 0);
        if (flags == -1) throw std::runtime_error("fcntl F_GETFL failed");
        if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1) throw std::runtime_error("fcntl F_SETFL failed");
    }

public:
    
    TCPServer(int tPort) :
        mSocket(-1),
        mPort(tPort)
    {

    }

    ~TCPServer() {
        if (mRunning) stop();
    }

    void start() {
        if (mRunning) {
            log.debug() << "TCPServer already running";
            return;
        }

        mSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (mSocket < 0) {
            throw std::runtime_error("socket failed");
        }

        int opt = 1;
        if (setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(mSocket);
            throw std::runtime_error("setsockopt failed");
        }

        sockaddr_in serverAddr{};
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY; 
        serverAddr.sin_port = htons(mPort);

        if (bind(mSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
            close(mSocket);
            throw std::runtime_error("bind failed");
        }


        if (listen(mSocket, SOMAXCONN) < 0) {
            close(mSocket);
            throw std::runtime_error("listen failed");
        }

        setNonBlocking(mSocket);

        mRunning = true;
        mListenerThread = std::thread(&TCPServer::run, this);

        log.info() << "TCPServer started on port " << mPort;
    }

    void stop() {
        if (!mRunning) {
            log.debug() << "TCPServer not running";
            return;
        }

        mRunning = false;
        if (mSocket >= 0) {
            close(mSocket);
            mSocket = -1;
        }

        log.debug() << "TCPServer waiting for listener to finish...";
        if (mListenerThread.joinable()) {
            mListenerThread.join();
        }

        log.debug() << "TCPServer stopped";
    }

    std::string statusString;
    bool connected() {
        mFutures.erase(std::remove_if(mFutures.begin(), mFutures.end(), [](const std::future<void>& f) {
            return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }), mFutures.end());
        return mFutures.size() > 0;
    }

private:

    void run() {
        fd_set read_fds;
        while (mRunning) {
            FD_ZERO(&read_fds);
            FD_SET(mSocket, &read_fds);

            int activity = select(mSocket + 1, &read_fds, nullptr, nullptr, nullptr);
            if (activity < 0) {
                if (errno != EINTR) {
                    log.error() << "TCPServer select error";
                    break;
                }
                continue;
            }

            if (FD_ISSET(mSocket, &read_fds)) {
                sockaddr_in clientAddr{};
                socklen_t addrLen = sizeof(clientAddr);

                int clientSocket = accept(mSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
                if (clientSocket < 0) {
                    if (mRunning) log.error() << "TCPServer accept failed: " << strerror(errno);
                    continue;
                }

                mFutures.emplace_back(std::async(std::launch::async, &TCPServer::handleClient, this, clientSocket, clientAddr));
                log.info() << "TCPServer accepted connection from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port);
            }
        }
    }

    void handleClient(int clientSocket, sockaddr_in clientAddr) {
        try {
            setNonBlocking(clientSocket);

            char buffer[1024];
            while (mRunning) {
                memset(buffer, 0, sizeof(buffer));
                ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytesRead > 0) {
                    auto response = std::string(buffer, bytesRead);
                    log.info() << "TCPServer received from client [" << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << "]: " << response;
                }

                if (send(clientSocket, statusString.c_str(), statusString.size(), 0) < 0) {
                    throw std::runtime_error("send failed");
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        catch (const std::exception& e) {
            log.error() << "TCPServer exception in handleClient: " << e.what();
        }

        close(clientSocket);
        log.info() << "TCPServer client [" << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << "] disconnected";
    }
};

}