#ifndef GAZELLEMQ_SERVER_SUBSCRIBERSERVICE_HPP
#define GAZELLEMQ_SERVER_SUBSCRIBERSERVICE_HPP


#include <thread>
#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "Consts.hpp"
#include "Message.hpp"
#include "MessageSubscriber.hpp"
#include "MessageQueue.hpp"


namespace gazellemq::server {
    class SubscriberService {
    private:
        std::vector<MessageSubscriber> subscribers;
        MessageQueue& messageQueue;
        std::atomic_flag hasNewSubscribers{false};
        std::atomic_flag isRunning{true};
        std::jthread bgThread;
    public:
        explicit SubscriberService():messageQueue(getMessageQueue()) {}
    private:
        /**
         * Error handler
         * @param msg
         */
        static void printError(char const* msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
        }

        /**
         * Removes all zombie subscribers
         */
        void killZombies() {
            subscribers.erase(
                std::remove_if(subscribers.begin(), subscribers.end(), [](MessageSubscriber const& o) {
                    if (o.getIsZombie()) {
                        printf("Remove zombie subscriber [%s]\n", o.clientName.c_str());
                    }
                    return o.getIsZombie();
                }), subscribers.end());
        }

        /**
         * Initializes all new subscribers
         * @param ring
         */
        void initNewSubscribers(io_uring* ring) {
            if (hasNewSubscribers.test()) {
                hasNewSubscribers.clear();
                std::for_each(subscribers.begin(), subscribers.end(), [ring](MessageSubscriber &o) {
                    return o.handleEvent(ring, 0);
                });
            }
        }

        /**
         * Removes inactive subscribers/publishers, and runs handleEvent(...) on new publishers/subscribers
         */
        void onBeforeHandleMessages(io_uring* ring) {
            killZombies();
            initNewSubscribers(ring);
        }
    public:
        void go() {
            using namespace std::chrono_literals;
            bgThread = std::jthread{[this]() {
                printf("SubscriberService running in background thread.\n");

                constexpr static size_t NB_EVENTS = 32;
                io_uring ring{};
                io_uring_queue_init(NB_EVENTS, &ring, 0);


                std::vector<io_uring_cqe*> cqes{};
                cqes.reserve(NB_EVENTS);
                cqes.insert(cqes.begin(), NB_EVENTS, nullptr);

                __kernel_timespec ts{.tv_sec = 2, .tv_nsec = 0};

                outer:
                while (isRunning.test()) {
                    messageQueue.afQueue.wait(false);

                    onBeforeHandleMessages(&ring);

                    MessageBatch batch;
                    while (messageQueue.try_pop(batch)) {
                        std::for_each(subscribers.begin(), subscribers.end(), [&batch, &ring](MessageSubscriber& subscriber) {
                            if (subscriber.isSubscribed(batch.getMessageType())) {
                                subscriber.pushMessageBatch(&ring, batch);
                            }
                        });
                    }

                    messageQueue.afQueue.clear();

                    while (isRunning.test()) {
                        bool hasZombies{};
                        bool allIdle{true};
                        std::for_each(subscribers.begin(), subscribers.end(), [&hasZombies, &allIdle](MessageSubscriber const& o) {
                            if (!o.isIdle()) allIdle = false;
                            if (o.getIsZombie()) hasZombies = true;
                        });

                        if (hasZombies) killZombies();
                        if (allIdle) goto outer;

                        int ret = io_uring_wait_cqe_timeout(&ring, cqes.data(), &ts);
                        if (ret == -SIGILL || ret == TIMEOUT) {
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
                                    auto *pSubscriber = static_cast<MessageSubscriber *>(io_uring_cqe_get_data(cqe));
                                    pSubscriber->handleEvent(&ring, res);
                                }
                                io_uring_cqe_seen(&ring, cqe);
                            }
                        });
                    }
                }
            }};
        }

        /**
         * Registers a subscriber so it can receive messages
         * @param name
         * @param fd
         */
        void newSubscriber(std::string &&name, int fd) {
            MessageSubscriber subscriber{fd, std::move(name)};

            // other subscribers with the same name will be removed
            for (MessageSubscriber& o: subscribers) {
                if (o.clientName == subscriber.clientName) {
                    o.markForRemoval();
                }
            }

            subscriber.printHello();
            subscribers.push_back(std::move(subscriber));
            hasNewSubscribers.test_and_set();
            messageQueue.afQueue.test_and_set();
            messageQueue.afQueue.notify_one();
        }
    };

    inline SubscriberService _msq{};

    static SubscriberService& getSubscriberService() {
        return gazellemq::server::_msq;
    }
}

#endif //GAZELLEMQ_SERVER_SUBSCRIBERSERVICE_HPP
