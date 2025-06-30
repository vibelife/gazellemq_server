#ifndef BASESERVER_HPP
#define BASESERVER_HPP
#include <cstring>
#include <thread>
#include <vector>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <algorithm>
#include <functional>

#include "BaseObject.hpp"
#include "PubSubHandler.hpp"
#include "Enums.hpp"

namespace gazellemq::server {
    class ServerContext;

    class BaseServer : public BaseObject {
    protected:
        static constexpr auto TIMEOUT = -62;

        int fd{};
        int port;
        int epfd{};
        struct sockaddr_in clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);
        std::vector<PubSubHandler*> clients{};
        Enums::Event event{Enums::Event::Event_NotSet};
        unsigned int maxEventBatch{8};
        std::jthread bgThread;
        ServerContext* serverContext{};
        std::atomic_flag& isRunning;
        std::function<PubSubHandler* (int, ServerContext*)> createHandlerFn{};
    public:
        BaseServer(
                int const port,
                ServerContext* serverContext,
                std::atomic_flag& isRunning,
                std::function<PubSubHandler* (int, ServerContext*)>&& createFn
            )
            :port(port), serverContext(serverContext), isRunning(isRunning), createHandlerFn(std::move(createFn))
        {}

        ~BaseServer() override {
            while (!clients.empty()) {
                delete clients.at(0);
                clients.erase(clients.begin());
            }
        }
    protected:
        void removeDisconnectedClients() {
            // TODO figure out why deleting disconnected clients eventually causes a segfault

            //clients.erase(
            //        std::remove_if(clients.begin(), clients.end(), [](T& o) {
            //            return !o.getIsNew() && o.getIsDisconnected();
            //        }), clients.end());
        }

        [[nodiscard]] bool anyNew() const {
            return std::any_of(clients.begin(), clients.end(), [](PubSubHandler const* o) {
                return o->getIsNew();
            });
        }

        [[nodiscard]] bool allIdle() const {
            return std::all_of(clients.begin(), clients.end(), [](PubSubHandler const* o) {
                return o->getIsIdle();
            });
        }


        /**
         * Error handler
         * @param msg
         */
        static void printError(char const* msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
        }

        /**
         * Sets up the listening socket
         * @param ring
         * @return
         */
        void beginSetupListenerSocket(struct io_uring *ring) {
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
            int ret = bind(fd, (const struct sockaddr *) &srv_addr, sizeof(srv_addr));
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


            io_uring_sqe *sqe = io_uring_get_sqe(ring);
            struct epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLERR | EPOLLRDHUP;
            ev.data.fd = fd;
            io_uring_prep_epoll_ctl(sqe, epfd, fd, EPOLL_CTL_ADD, &ev);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event::Event_SetupPublisherListeningSocket;
            io_uring_submit(ring);
        }

        /**
         * Completes the setup event and goes to the accept event
         * @param res
         */
        void onSetupListeningSocketComplete(struct io_uring *ring, int res) {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__, res);
            } else {
                printHello();
                beginAcceptConnection(ring);
            }
        }

        /**
         * Submits an [accept] system call using liburing
         * @param ring
         */
        void beginAcceptConnection(struct io_uring *ring) {
            io_uring_sqe *sqe = io_uring_get_sqe(ring);
            io_uring_prep_accept(sqe, fd, (sockaddr *) &clientAddr, &clientAddrLen, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event::Event_AcceptPublisherConnection;
            io_uring_submit(ring);
        }

        /**
         * Accepts an incoming connection, then makes the connection non blocking.
         * @param ring
         * @param res
         */
        void onAcceptConnectionComplete(struct io_uring *ring, int res) {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__, res);
            } else {
                // listen for more connections
                beginAcceptConnection(ring);

                // A client has connected
                auto client = createHandlerFn(res, serverContext);
                clients.emplace_back(client);
                afterConnectionAccepted(ring, client);
            }
        }

        virtual void printHello() = 0;

        virtual void afterConnectionAccepted(struct io_uring *ring, PubSubHandler* connection) = 0;

        virtual void doEventLoop(io_uring* ring) = 0;
    public:
        void start() {
            bgThread = std::jthread{[this]() {
                struct io_uring ring{};
                io_uring_queue_init(8, &ring, 0);
                handleEvent(&ring, 0);

                doEventLoop(&ring);
            }};
        }

        std::vector<PubSubHandler*> getClients() {
            return clients;
        }
    };
}

#endif //BASESERVER_HPP
