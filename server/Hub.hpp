#ifndef GAZELLEMQ_HUB_HPP
#define GAZELLEMQ_HUB_HPP

#include <vector>
#include <csignal>
#include <cstdio>
#include <liburing.h>
#include <netinet/in.h>
#include <cstring>

#include "MessagePublisher.hpp"
#include "MessageSubscriber.hpp"
#include "Consts.hpp"
#include "EventLoopObject.hpp"
#include "ServerConnection.hpp"

namespace gazellemq::server {
    class Hub {
    private:
        static constexpr auto NB_EVENTS = 32;
        ServerConnection* serverConnection{};
        struct io_uring ring{};

    public:
        ~Hub() {
            delete serverConnection;
        }

        void start(int port, size_t const queueDepth) {
            signal(SIGINT, sigintHandler);

            getPushService().go();

            io_uring_queue_init(queueDepth, &ring, 0);

            serverConnection = new ServerConnection{port};
            serverConnection->handleEvent(&ring, 0);

            doEventLoop();
        }

    private:
        /**
         * Error handler
         * @param msg
         */
        static void printError(char const* msg) {
            printf("Error: %s\n", msg);
        }

        /**
         * Error handler
         * @param msg
         */
        static void printError(char const* msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
        }

        /**
         * Error handler
         * @param msg
         */
        static void fatalError(char const* msg) {
            printf("%s\n", msg);
            exit(1);
        }

        /**
         * Error handler
         * @param msg
         */
        static void fatalError(char const* msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
            exit(1);
        }

        /**
         * Signal handler
         * @param signo
         */
        static void sigintHandler(int signo) {
            printf("^C pressed. Shutting down\n");
            exit(0);
        }

        /**
         * Does the event loop
         */
        void doEventLoop() {
            std::vector<io_uring_cqe *> cqes{};
            cqes.reserve(NB_EVENTS);
            cqes.insert(cqes.begin(), NB_EVENTS, nullptr);

            __kernel_timespec ts{.tv_sec = 2, .tv_nsec = 0};

            while (true) {
                int ret = io_uring_wait_cqe_timeout(&ring, cqes.data(), &ts);
                if (ret == -SIGILL) {
                    continue;
                }

                if (ret < 0) {
                    if (ret == TIMEOUT) {
                        continue;
                    } else {
                        printError("io_uring_wait_cqe_timeout(...)", ret);
                        return;
                    }
                }

                for (auto *&cqe: cqes) {
                    if (cqe != nullptr) {
                        int res = cqe->res;
                        if (res == -EAGAIN) {
                            io_uring_cqe_seen(&ring, cqe);
                            continue;
                        }

                        auto* pObject = static_cast<EventLoopObject*>(io_uring_cqe_get_data(cqe));
                        pObject->handleEvent(&ring, res);
                    }
                    io_uring_cqe_seen(&ring, cqe);
                }
            }
        }
    };
}

#endif //GAZELLEMQ_HUB_HPP
