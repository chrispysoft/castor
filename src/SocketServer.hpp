#include <iostream>
#include <thread>
#include <atomic>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

namespace lap {
class SocketServer {

    std::atomic<int> mSocket;
    std::string mSocketPath;
    std::atomic<bool> mRunning;
    std::thread listenerThread;

public:
    // Constructor
    SocketServer(const std::string& socketPath) :
        mSocket(-1),
        mSocketPath(socketPath),
        mRunning(false)
    {}

    ~SocketServer() {
        stop();
    }


    void start() {
        if (mRunning.load()) {
            std::cerr << "Server is already running!" << std::endl;
            return;
        }

        mRunning.store(true);

        // Create a Unix Domain Socket
        mSocket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (mSocket < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        // Set the socket to non-blocking mode
        int flags = fcntl(mSocket, F_GETFL, 0);
        if (flags == -1) {
            throw std::runtime_error("fcntl(F_GETFL) failed");
        }

        if (fcntl(mSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
            throw std::runtime_error("fcntl(F_SETFL) failed to set non-blocking");
        }

        // Bind the socket to the file path
        sockaddr_un serverAddr{};
        serverAddr.sun_family = AF_UNIX;
        strncpy(serverAddr.sun_path, mSocketPath.c_str(), sizeof(serverAddr.sun_path) - 1);

        // Remove any existing socket file
        unlink(mSocketPath.c_str());

        if (bind(mSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            close(mSocket);
            throw std::runtime_error("Bind failed");
        }

        // Start listening for connections
        if (listen(mSocket, 5) < 0) {
            close(mSocket);
            throw std::runtime_error("Listen failed");
        }

        // Start the listener thread
        listenerThread = std::thread(&SocketServer::run, this);

        std::cout << "Server started on socket: " << mSocketPath << std::endl;
    }

    // Stop the server
    void stop() {
        if (!mRunning.load()) {
            return;
        }

        mRunning.store(false);

        std::cout << "Closing socket..." << std::endl;
        if (mSocket >= 0) {
            close(mSocket);
            mSocket.exchange(-1);
        }

        // std::cout << "Waiting for listener to finish..." << std::endl;
        if (listenerThread.joinable()) {
            listenerThread.join();
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

            // Check if server socket is ready to accept
            if (fds[0].revents & POLLIN) {
                sockaddr_un clientAddr{};
                socklen_t clientLen = sizeof(clientAddr);

                // Accept a new client connection
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

    // Handle client communication
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

            std::cout << "Received: " << buffer << std::endl;
            
            send(clientSocket, buffer, bytesRead, 0);
        }
    }
};

}
