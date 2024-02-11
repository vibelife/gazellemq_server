#ifndef GAZELLEMQ_SERVER_PUSHSERVICE_HPP
#define GAZELLEMQ_SERVER_PUSHSERVICE_HPP


#include <thread>
#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "Message.hpp"
#include "MessageSubscriber.hpp"
#include "Consts.hpp"
#include "MessageChunk.hpp"


namespace gazellemq::server {
    class PushService {
    private:
        std::vector<MessageSubscriber*> subscribers;
        rigtorp::MPMCQueue<MessageChunk> messageQueue;
        std::atomic_flag afQueue{false};
        std::atomic_flag isRunning{true};
        std::jthread bgThread;
    public:
        explicit PushService(size_t messageQueueDepth)
            :messageQueue(messageQueueDepth)
        {}
    private:
        /**
         * Error handler
         * @param msg
         */
        static void printError(char const* msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
        }

    public:
        /**
         * Pushes the message to the queue. Messages are sent to subscribers in a background thread.
         * @param messageType
         * @param messageContent
         */
        void pushToQueue(std::string const& messageType, char const* buffer, size_t bufferLen) {
            messageQueue.emplace(MessageChunk{messageType, buffer, bufferLen});
            notify();
        }

        /*
        void pushToQueue(std::string&& messageType, std::string&& messageContent) {
            auto* message = new Message{};

            message->messageType.append(messageType);

            message->content.append(messageType);
            message->content.append("|");
            message->content.append(std::to_string(messageContent.size()));
            message->content.append("|");
            message->content.append(messageContent);

            message->n = message->content.size();
            message->i = 0;

            messageQueue.push(message);

            notify();
        }
        */

        /**
         * Alerts the thread that there is pending data
         */
        void notify() {
            //if (!hasPendingData.test()) {
            //    std::lock_guard lock{mQueue};
            //    hasPendingData.test_and_set();
            //    cvQueue.notify_one();
            //}
            if (!afQueue.test()) {
                afQueue.test_and_set();
                afQueue.notify_one();
            }
        }

        /**
         * Removes inactive subscribers
         */
        void doSubscriberCleanUp() {
            bool mustRemove{};
            size_t i{subscribers.size()};
            while (i-- > 0) {
                if (subscribers[i]->getIsZombie()) {
                    subscribers[i] = nullptr;
                    mustRemove = true;
                }
            }

            if (mustRemove) {
                subscribers.erase(
                        std::remove_if(subscribers.begin(), subscribers.end(), [](MessageSubscriber *o) {
                            return o == nullptr;
                        }), subscribers.end());
            }
        }


        void go() {
            using namespace std::chrono_literals;
            bgThread = std::jthread{[this]() {
                constexpr static size_t NB_EVENTS = 32;
                io_uring ring{};
                io_uring_queue_init(NB_EVENTS, &ring, 0);


                std::vector<io_uring_cqe*> cqes{};
                cqes.reserve(NB_EVENTS);
                cqes.insert(cqes.begin(), NB_EVENTS, nullptr);

                __kernel_timespec ts{.tv_sec = 2, .tv_nsec = 0};

                outer:
                while (isRunning.test()) {
                    afQueue.wait(false);

                    doSubscriberCleanUp();

                    bool isPushing{};
                    MessageChunk message;
                    while (messageQueue.try_pop(message)) {
                        for (auto* subscriber : subscribers) {
                            if (subscriber->isSubscribed(message.messageType)) {
                                subscriber->push(&ring, message);
                                isPushing = true;
                            }
                        }
                    }

                    afQueue.clear();

                    if (!isPushing) {
                        goto outer;
                    }

                    while (isRunning.test()) {
                        if (std::all_of(subscribers.begin(), subscribers.end(), [](MessageSubscriber* o) {
                            return o->isIdle();
                        })) {
                            goto outer;
                        }
                        int ret = io_uring_wait_cqe_timeout(&ring, cqes.data(), &ts);
                        if (ret == -SIGILL || ret == TIMEOUT) {
                            continue;
                        }

                        if (ret < 0) {
                            printError("io_uring_wait_cqe_timeout(...)", ret);
                            goto outer;
                        }

                        for (auto& cqe: cqes) {
                            if (cqe != nullptr) {
                                int res = cqe->res;
                                if (res == -EAGAIN) {
                                    io_uring_cqe_seen(&ring, cqe);
                                    continue;
                                }

                                auto* pSubscriber = static_cast<MessageSubscriber*>(io_uring_cqe_get_data(cqe));
                                pSubscriber->handleEvent(&ring, res);
                            }
                            io_uring_cqe_seen(&ring, cqe);
                        }

                    }
                }
            }};
        }

        /**
         * Registers a subscriber so it can receive messages
         * @param subscriber
         */
        void registerSubscriber(MessageSubscriber* subscriber) {
            // other subscribers with the same name will be removed
            for (auto* o: subscribers) {
                if (o->clientName == subscriber->clientName) {
                    o->markForRemoval();
                }
            }

            subscribers.emplace_back(subscriber);
        }
    };

    inline PushService _mq{500000};

    static PushService& getPushService() {
        return gazellemq::server::_mq;
    }
}

#endif //GAZELLEMQ_SERVER_PUSHSERVICE_HPP
