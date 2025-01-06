#pragma once

#include <iostream>
#include <cstring>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include "../util/Log.hpp"

namespace cst {
class SocketServer {

    int mSocket;
    std::string mSocketPath;
    std::atomic<bool> mRunning;
    std::thread mListenerThread;
    std::mutex mSocketMutex;

public:
    using TxCallback = std::function<void(const std::string& msg)>;
    using RxHandler = std::function<void(const char* data, size_t len, TxCallback txCallback)>;

    RxHandler rxHandler;

    SocketServer(const std::string& tSocketPath) :
        mSocket(-1),
        mSocketPath(tSocketPath),
        mRunning(false)
    {
        if (mSocketPath.size() >= sizeof(sockaddr_un::sun_path)) {
            throw std::runtime_error("socket path too long");
        }
    }

    ~SocketServer() {
        if (mRunning) stop();
    }

    void start() {
        if (mRunning) {
            log.debug() << "SocketServer already running";
            return;
        }

        // create UDS
        mSocket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (mSocket < 0) {
            throw std::runtime_error("socket failed");
        }

        // non-blocking
        int flags = fcntl(mSocket, F_GETFL, 0);
        if (flags == -1 || fcntl(mSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
            close(mSocket);
            throw std::runtime_error("failed to set socket non-blocking");
        }

        if (fcntl(mSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
            close(mSocket);
            throw std::runtime_error("fcntl failed to set non-blocking");
        }

        // remove any existing socket file
        unlink(mSocketPath.c_str());

        // bind to file path
        sockaddr_un serverAddr{};
        serverAddr.sun_family = AF_UNIX;
        std::strncpy(serverAddr.sun_path, mSocketPath.c_str(), sizeof(serverAddr.sun_path) - 1);

        if (bind(mSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
            close(mSocket);
            throw std::runtime_error("bind failed");
        }

        // start listening for connections
        if (listen(mSocket, 5) < 0) {
            close(mSocket);
            throw std::runtime_error("listen failed");
        }

        mRunning = true;
        mListenerThread = std::thread(&SocketServer::run, this);

        log.info() << "SocketServer started on socket '" << mSocketPath << "'";
    }

    void stop() {
        if (!mRunning) {
            log.debug() << "SocketServer not running";
            return;
        }

        mRunning = false;

        {
            std::lock_guard<std::mutex> lock(mSocketMutex);
            if (mSocket >= 0) {
                close(mSocket);
                mSocket = -1;
            }
        }

        log.debug() << "SocketServer waiting for listener to finish...";
        if (mListenerThread.joinable()) {
            mListenerThread.join();
        }

        log.debug() << "SocketServer unlinking socket...";
        unlink(mSocketPath.c_str());
        
        log.debug() << "SocketServer stopped";
    }

    std::string statusString;

private:

    void run() {
        while (mRunning) {
            pollfd fds[1];
            {
                std::lock_guard<std::mutex> lock(mSocketMutex);
                if (mSocket < 0) break; // socket closed during stop()
                fds[0].fd = mSocket;
            }
            fds[0].events = POLLIN;

            int ret = poll(fds, 1, 100); // avoid busy-waiting
            if (ret < 0) {
                if (errno == EINTR) continue;
                log.error() << "SocketServer poll failed: " << strerror(errno);
                break;
            }

            if (ret > 0 && (fds[0].revents & POLLIN)) {
                sockaddr_un clientAddr{};
                socklen_t clientLen = sizeof(clientAddr);

                int clientSocket = accept(mSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
                if (clientSocket < 0) {
                    if (errno == EINTR) continue; // retry if interrupted
                    log.error() << "SocketServer accept failed: " << strerror(errno);
                    continue;
                }

                log.info() << "SocketServer client connected";
                std::thread(&SocketServer::handleClient, this, clientSocket).detach();
            }
        }
    }

    void handleClient(int clientSocket) {
        char buffer[1024];

        try {
            while (true) {
                memset(buffer, 0, sizeof(buffer));
                ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

                // if (bytesRead <= 0) {
                //     if (bytesRead < 0) {
                //         log.error() << "SocketServer recv failed: " << strerror(errno);
                //     }
                //     break;
                // }

                // if (rxHandler) {
                //     rxHandler(buffer, static_cast<size_t>(bytesRead), [clientSocket](const std::string& response) {
                //         if (send(clientSocket, response.c_str(), response.size(), 0) < 0) {
                //             log.error() << "SocketServer send failed: " << strerror(errno);
                //         }
                //     });
                // }
                // log.debug() << response;
                if (send(clientSocket, statusString.c_str(), statusString.size(), 0) < 0) {
                    log.error() << "SocketServer send failed: " << strerror(errno);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        catch (const std::exception& e) {
            log.error() << "SocketServer exception in client handler: " << e.what();
        }
        
        close(clientSocket);
        log.info() << "Client disconnected";
    }
};

}
