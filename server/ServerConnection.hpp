#ifndef GAZELLEMQ_SERVERCONNECTION_HPP
#define GAZELLEMQ_SERVERCONNECTION_HPP

#include <sys/socket.h>
#include <sys/epoll.h>

#include <vector>
#include <cstdlib>
#include <cstdio>
#include <netinet/in.h>
#include <cstring>
#include <liburing/io_uring.h>
#include <liburing.h>
#include "EventLoopObject.hpp"
#include "ClientConnection.hpp"

namespace gazellemq::server {
    class ServerConnection : public EventLoopObject {
    private:
        int fd{};
        int port{};
        int epfd{};
        struct sockaddr_in clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);
        std::vector<ClientConnection*> clientConnections{};

    private:
        /**
         * Sets up the listening socket
         * @param ring
         * @return
         */
        void beginSetupListenerSocket(struct io_uring* ring) {
            fd = socket(PF_INET, SOCK_STREAM, 0);
            if (fd == -1) {
                printf("%s\n", "socket(...)");
                exit(1);
            }

            struct sockaddr_in srv_addr{};
            memset(&srv_addr, 0, sizeof(srv_addr));

            srv_addr.sin_family = AF_INET;
            srv_addr.sin_port = htons(port);
            srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

            int optVal = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));

            // We bind to a port and turn this socket into a listening socket.
            int ret = bind(fd, (const struct sockaddr*) &srv_addr, sizeof(srv_addr));
            if (ret < 0) {
                printf("%s\n", "bind(...)");
                exit(1);
            }

            ret = listen(fd, 10);
            if (ret < 0) {
                printf("%s\n", "listen(...)");
                exit(1);
            }

            // set up polling of the inotify
            epfd = epoll_create1(0);
            if (epfd < 0) {
                printf("%s\n", "epoll_create1(...)");
                exit(1);
            }


            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            struct epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLERR | EPOLLRDHUP;
            ev.data.fd = fd;
            io_uring_prep_epoll_ctl(sqe, epfd, fd, EPOLL_CTL_ADD, &ev);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            event = Enums::Event::Event_SetupPublisherListeningSocket;
            io_uring_submit(ring);
        }

        /**
         * Completes the setup event and goes to the accept event
         * @param res
         */
        void onSetupListeningSocketComplete(struct io_uring* ring, int res) {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__ , res);
            } else {
                beginAcceptConnection(ring);
            }
        }

        /**
         * Submits an [accept] system call using liburing
         * @param ring
         */
        void beginAcceptConnection(struct io_uring* ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_accept(sqe, fd, (sockaddr*) &clientAddr, &clientAddrLen, 0);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            event = Enums::Event::Event_AcceptPublisherConnection;
            io_uring_submit(ring);
        }

        /**
         * Accepts an incoming connection, then makes the connection non blocking.
         * @param ring
         * @param res
         */
        void onAcceptConnectionComplete(struct io_uring* ring, int res) {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__ , res);
            } else {
                // listen for more connections
                beginAcceptConnection(ring);

                // A publisher has connected
                auto* client = new ClientConnection();
                clientConnections.emplace_back(client);
                client->start(ring, epfd, res);
            }
        }

    public:
        ServerConnection(int port): port(port) {}

        ~ServerConnection() {
            std::for_each(clientConnections.begin(), clientConnections.end(), [](auto& o) {delete o;});
            clientConnections.clear();
        }

    public:
        void handleEvent(struct io_uring *ring, int res) override {
            switch (event) {
                case Enums::Event::Event_NotSet:
                    beginSetupListenerSocket(ring);
                    break;
                case Enums::Event::Event_SetupPublisherListeningSocket:
                    onSetupListeningSocketComplete(ring, res);
                    break;
                case Enums::Event::Event_AcceptPublisherConnection:
                    onAcceptConnectionComplete(ring, res);
                    break;
                default:
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_SERVERCONNECTION_HPP
