#pragma once

#include <iostream>
#include <thread>
#include <atomic>
#include <cstring>
#include <functional>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

namespace lap {
class SocketServer {

    int mSocket;
    std::string mSocketPath;
    std::atomic<bool> mRunning;
    std::thread mListenerThread;

public:

    typedef std::function<void(const std::string& msg)> TxCallback;
    typedef std::function<void(const char* data, size_t len, TxCallback txCallback)> RxHandler;
    RxHandler rxHandler;

    SocketServer(const std::string& tSocketPath) :
        mSocket(-1),
        mSocketPath(tSocketPath),
        mRunning(false)
    {}

    ~SocketServer() {
        stop();
    }


    void start() {
        if (mRunning.load()) {
            std::cerr << "SocketServer already running" << std::endl;
            return;
        }

        mRunning.store(true);

        // create UDS
        mSocket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (mSocket < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        // non-blocking mode
        int flags = fcntl(mSocket, F_GETFL, 0);
        if (flags == -1) {
            throw std::runtime_error("fcntl failed");
        }

        if (fcntl(mSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
            throw std::runtime_error("fcntl failed to set non-blocking");
        }

        // bind to file path
        sockaddr_un serverAddr{};
        serverAddr.sun_family = AF_UNIX;
        strncpy(serverAddr.sun_path, mSocketPath.c_str(), sizeof(serverAddr.sun_path) - 1);

        // remove any existing socket file
        unlink(mSocketPath.c_str());

        if (bind(mSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            close(mSocket);
            throw std::runtime_error("Bind failed");
        }

        // start listening for connections
        if (listen(mSocket, 5) < 0) {
            close(mSocket);
            throw std::runtime_error("Listen failed");
        }

        // start listener thread
        mListenerThread = std::thread(&SocketServer::run, this);

        std::cout << "SocketServer started on socket '" << mSocketPath << "'" << std::endl;
    }

    void stop() {
        if (!mRunning.load()) {
            std::cerr << "SocketServer not running" << std::endl;
            return;
        }

        mRunning.store(false);

        // std::cout << "Closing socket..." << std::endl;
        if (mSocket >= 0) {
            close(mSocket);
            mSocket = -1;
        }

        // std::cout << "Waiting for listener to finish..." << std::endl;
        if (mListenerThread.joinable()) {
            mListenerThread.join();
        }

        // std::cout << "Unlinking socket..." << std::endl;
        unlink(mSocketPath.c_str());
        
        std::cout << "SocketServer stopped" << std::endl;
    }

private:

    void run() {
        while (mRunning.load()) {
            pollfd fds[1];
            fds[0].fd = mSocket;
            fds[0].events = POLLIN;

            int ret = poll(fds, 1, 0);
            if (ret < 0) {
                if (errno == EINTR) continue;
                std::cout << "Poll failed" << std::endl;
                break;
            }

            // check if server socket is ready to accept
            if (fds[0].revents & POLLIN) {
                sockaddr_un clientAddr{};
                socklen_t clientLen = sizeof(clientAddr);

                // accept new client connection
                int clientSocket = accept(mSocket, (struct sockaddr*)&clientAddr, &clientLen);
                if (clientSocket < 0) {
                    if (errno == EINTR) continue;  // Retry if interrupted
                    std::cout << "Accept failed" << std::endl;
                    continue;
                }

                std::cout << "Client connected" << std::endl;
                std::thread(&SocketServer::handleClient, this, clientSocket).detach();
            }
        }
    }

    void handleClient(int clientSocket) {
        char buffer[1024];
        while (true) {
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

            if (bytesRead <= 0) {
                std::cout << "Client disconnected" << std::endl;
                close(clientSocket);
                break;
            }

            // std::cout << "Received: " << buffer << std::endl;

            if (rxHandler) {
                rxHandler(buffer, bytesRead, [clientSocket](const auto& response) {
                    // std::cout << "Sending response '" << response << "'" << std::endl;
                    send(clientSocket, response.c_str(), response.size(), 0);
                });
            }
            
            // send(clientSocket, buffer, bytesRead, 0);
        }
    }
};

}
