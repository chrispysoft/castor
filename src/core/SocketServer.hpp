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
            throw std::runtime_error("Socket path too long");
        }
    }

    ~SocketServer() {
        if (mRunning) stop();
    }

    void start() {
        if (mRunning) {
            std::cerr << "SocketServer already running" << std::endl;
            return;
        }

        // create UDS
        mSocket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (mSocket < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        // non-blocking
        int flags = fcntl(mSocket, F_GETFL, 0);
        if (flags == -1 || fcntl(mSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
            close(mSocket);
            throw std::runtime_error("Failed to set socket non-blocking");
        }

        if (fcntl(mSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
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
            throw std::runtime_error("Bind failed");
        }

        // start listening for connections
        if (listen(mSocket, 5) < 0) {
            close(mSocket);
            throw std::runtime_error("Listen failed");
        }

        mRunning = true;
        mListenerThread = std::thread(&SocketServer::run, this);

        std::cout << "SocketServer started on socket '" << mSocketPath << "'" << std::endl;
    }

    void stop() {
        if (!mRunning) {
            std::cerr << "SocketServer not running" << std::endl;
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
                std::cerr << "Poll failed: " << strerror(errno) << std::endl;
                break;
            }

            if (ret > 0 && (fds[0].revents & POLLIN)) {
                sockaddr_un clientAddr{};
                socklen_t clientLen = sizeof(clientAddr);

                int clientSocket = accept(mSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
                if (clientSocket < 0) {
                    if (errno == EINTR) continue; // retry if interrupted
                    std::cerr << "Accept failed: " << strerror(errno) << std::endl;
                    continue;
                }

                std::cout << "Client connected" << std::endl;
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

                if (bytesRead <= 0) {
                    if (bytesRead < 0) {
                        std::cerr << "Error reading from client: " << strerror(errno) << std::endl;
                    }
                    break;
                }

                if (rxHandler) {
                    rxHandler(buffer, static_cast<size_t>(bytesRead), [clientSocket](const std::string& response) {
                        if (send(clientSocket, response.c_str(), response.size(), 0) < 0) {
                            std::cerr << "Error sending response: " << strerror(errno) << std::endl;
                        }
                    });
                }
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Exception in client handler: " << e.what() << std::endl;
        }
        
        close(clientSocket);
        std::cout << "Client disconnected" << std::endl;
    }
};

}
