#ifndef GAZELLEMQ_SERVER_PUBLISHERSERVICE_HPP
#define GAZELLEMQ_SERVER_PUBLISHERSERVICE_HPP

#include <vector>
#include <cstring>
#include <csignal>
#include <linux/time_types.h>
#include <liburing/io_uring.h>
#include <liburing.h>

#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "MessagePublisher.hpp"
#include "Message.hpp"
#include "MessageQueue.hpp"

namespace gazellemq::server {
    class PublisherService {
    private:
        std::vector<MessagePublisher> publishers;
        MessageQueue& messageQueue;
        std::atomic_flag hasNewPublishers{false};
        std::atomic_flag isRunning{true};
        std::jthread bgThread;
    public:
        explicit PublisherService():messageQueue(getMessageChunkQueue()) {}
    private:
        /**
         * Error handler
         * @param msg
         */
        static void printError(char const *msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
        }

        /**
         * Pushes the message to the queue. Messages are sent to subscribers in a background thread.
         * @param messageType
         * @param messageContent
         */
        void pushToQueue(std::string const &messageType, std::string && buffer) {
            messageQueue.push_back(Message{messageType, buffer});
        }

        /**
         * Removes inactive subscribers/publishers, and runs handleEvent(...) on new publishers/subscribers
         */
        void onBeforeHandleMessages(io_uring* ring) {
            std::for_each(publishers.begin(), publishers.end(), [ring](MessagePublisher& o) {
                if (o.getMustDisconnect()) {
                    printf("Disconnecting publisher [%s]\n", o.clientName.c_str());
                    o.disconnect(ring);
                }
            });

            publishers.erase(
                    std::remove_if(publishers.begin(), publishers.end(), [](MessagePublisher const& o) {
                        if (o.getIsZombie()) {
                            printf("Remove zombie publisher [%s]\n", o.clientName.c_str());
                        }
                        return o.getIsZombie();
                    }), publishers.end());

            if (hasNewPublishers.test()) {
                hasNewPublishers.clear();
                std::for_each(publishers.begin(), publishers.end(), [ring](MessagePublisher &o) {
                    return o.handleEvent(ring, 0);
                });
            }
        }
    public:
        /**
         * Registers a publisher so it can sendMessage messages
         * @param name
         * @param fd
         */
        void newPublisher(std::string &&name, int fd) {
            MessagePublisher publisher{fd, std::move(name), [this](std::string const& messageType, std::string && buffer) {
                this->pushToQueue(messageType, std::move(buffer));
            }};

            // other publishers with the same name will be removed
            for (MessagePublisher& o: publishers) {
                if (o.clientName == publisher.clientName) {
                    o.forceDisconnect();
                }
            }

            publisher.printHello();
            publishers.push_back(std::move(publisher));
            hasNewPublishers.test_and_set();
        }

        void go() {
            using namespace std::chrono_literals;
            bgThread = std::jthread{[this]() {
                printf("PublisherService running in background thread.\n");

                constexpr static size_t NB_EVENTS = 32;
                io_uring ring{};
                io_uring_queue_init(NB_EVENTS, &ring, 0);


                std::vector<io_uring_cqe*> cqes{};
                cqes.reserve(NB_EVENTS);
                cqes.insert(cqes.begin(), NB_EVENTS, nullptr);

                __kernel_timespec ts{.tv_sec = 2, .tv_nsec = 0};

                outer:
                while (isRunning.test()) {
                    int ret = io_uring_wait_cqe_timeout(&ring, cqes.data(), &ts);
                    if (ret == -SIGILL || ret == TIMEOUT) {
                        onBeforeHandleMessages(&ring);
                        continue;
                    }

                    if (ret < 0) {
                        printError("io_uring_wait_cqe_timeout(...)", ret);
                        goto outer;
                    }

                    std::for_each(cqes.begin(), cqes.end(), [&ring](io_uring_cqe* cqe) {
                        if (cqe != nullptr) {
                            int res = cqe->res;
                            if (res != -EAGAIN) {
                                auto *pSubscriber = static_cast<MessagePublisher *>(io_uring_cqe_get_data(cqe));
                                pSubscriber->handleEvent(&ring, res);
                            }
                            io_uring_cqe_seen(&ring, cqe);
                        }
                    });
                }
            }};
        }
    };

    inline PublisherService _mpq{};

    static PublisherService& getPublisherService() {
        return gazellemq::server::_mpq;
    }
}

#endif //GAZELLEMQ_SERVER_PUBLISHERSERVICE_HPP
