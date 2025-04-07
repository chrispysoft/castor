/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Affero General Public License.
 */

#pragma once

#include <queue>
#include <cstring>
#include <atomic>
#include <thread>
#include <mutex>
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

namespace castor {
namespace io {
class TCPServer {

    int mSocket;
    int mPort;
    std::atomic<bool> mRunning = false;
    std::atomic<bool> mConnected = false;
    std::thread mListenerThread;
    std::queue<std::string> mStatusQueue;
    std::mutex mStatusMutex;
    std::mutex mSocketMutex;
    std::vector<std::future<void>> mFutures;

    void setNonBlocking(int socket) const {
        int flags = fcntl(socket, F_GETFL, 0);
        if (flags == -1) throw std::runtime_error("fcntl F_GETFL failed");
        if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1) throw std::runtime_error("fcntl F_SETFL failed");
    }

public:

    std::function<void(const std::string&)> onDataReceived;
    
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

        if (mPort <= 0 || mPort > UINT16_MAX) {
            throw std::runtime_error("invalid port "+std::to_string(mPort));
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

    void pushStatus(std::string tStatus) {
        std::lock_guard<std::mutex> lock(mStatusMutex);
        mStatusQueue.push(tStatus);
    }

    bool connected() {
        return mConnected;
    }

    std::string welcomeMessage = "Welcome to Castor!";

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
                log.error() << "TCPServer poll failed: " << strerror(errno);
                break;
            }

            if (fds[0].revents & POLLIN) {
                sockaddr_in clientAddr{};
                socklen_t clientLen = sizeof(clientAddr);

                int clientSocket = accept(mSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
                if (clientSocket < 0) {
                    if (errno == EINTR) continue; // interrupted
                    log.error() << "TCPServer accept failed: " << strerror(errno);
                    continue;
                }

                mFutures.emplace_back(std::async(std::launch::async, &TCPServer::handleClient, this, clientSocket, clientAddr));
                log.info() << "TCPServer accepted connection from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port);
            }

            if (mFutures.size()) {
                mFutures.erase(std::remove_if(mFutures.begin(), mFutures.end(), [](const std::future<void>& f) {
                    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                }), mFutures.end());
            }

            mConnected = mFutures.size() > 0;
        }
    }

    void handleClient(int clientSocket, sockaddr_in clientAddr) {
        try {
            setNonBlocking(clientSocket);

            if (send(clientSocket, welcomeMessage.c_str(), welcomeMessage.size(), 0) < 0) {
                throw std::runtime_error("send failed");
            }

            static constexpr size_t rxBufSz = 128;
            char rxBuf[rxBufSz];
            while (mRunning) {
                memset(rxBuf, 0, rxBufSz);
                ssize_t bytesRead = recv(clientSocket, rxBuf, rxBufSz - 1, 0);
                if (bytesRead > 0) {
                    auto response = std::string(rxBuf, bytesRead);
                    log.info() << "TCPServer received from client [" << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << "]: " << response;
                    if (onDataReceived) onDataReceived(response);
                }
                if (mStatusQueue.size()) {
                    std::string status;
                    {
                        std::lock_guard<std::mutex> lock(mStatusMutex);
                        status = mStatusQueue.front();
                        mStatusQueue.pop();
                    }

                    if (send(clientSocket, status.c_str(), status.size(), 0) < 0) {
                        throw std::runtime_error("send failed");
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
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
}