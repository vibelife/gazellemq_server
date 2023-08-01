#ifndef GAZELLEMQ_SERVER_PUSHSERVICE_HPP
#define GAZELLEMQ_SERVER_PUSHSERVICE_HPP

#include <condition_variable>
#include <thread>
#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "Message.hpp"
#include "MessageSubscriber.hpp"
#include "Consts.hpp"


namespace gazellemq::server {
    class PushService {
    private:
        std::vector<MessageSubscriber*> subscribers;
        rigtorp::MPMCQueue<Message*> messageQueue;

        std::mutex mQueue;
        std::condition_variable cvQueue;
        std::atomic_flag hasPendingData{false};
        std::atomic_flag isRunning{true};
        std::jthread bgThread;
        size_t count{};
    public:
        PushService()
            :messageQueue(500000)
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
         * Pushes the message out to subscribers.
         * @param ring
         * @param messageType
         * @param messageContent
         */
        void pushToSubscribers(struct io_uring *ring, std::string&& messageType, std::string&& messageContent) {
            std::string msg;
            msg.reserve(messageType.size() + messageContent.size() + 16);

            msg.append(messageType);
            msg.push_back('|');
            msg.append(std::to_string(messageContent.size()));
            msg.push_back('|');
            msg.append(messageContent);

            // for (auto* subscriber : subscribers) {
            //     if (subscriber->isSubscribed(messageType)) {
            //         auto* message = new Message{};
            //         message->content.reserve(msg.size());
            //         message->content.append(msg);
            //         subscriber->push(ring, message);
            //     }
            // }
        }

        /**
         * Pushes the message to the queue. Messages are sent to subscribers in a background thread.
         * @param messageType
         * @param messageContent
         */
        void pushToQueue(std::string&& messageType, std::string&& messageContent) {
            auto* message = new Message{};
            // message->messageType.append(messageType);
            // message->content.reserve(messageType.size() + messageContent.size() + 16);

            // message->content.append(messageType);
            // message->content.push_back('|');
            // message->content.append(std::to_string(messageContent.size()));
            // message->content.push_back('|');
            // message->content.append(messageContent);

            message->messageType = static_cast<char*>(calloc(messageType.size() + 1, sizeof(char)));
            memmove(message->messageType, messageType.c_str(), messageType.size());

            message->content = static_cast<char*>(calloc(messageType.size() + messageContent.size() + 3, sizeof(char)));

            std::string sz = std::to_string(messageContent.size());

            memmove(message->content, messageType.c_str(), messageType.size());
            memmove(&message->content[messageType.size()], "|", 1);
            memmove(&message->content[messageType.size() + 1], sz.c_str(), sz.size());
            memmove(&message->content[messageType.size() + 1 + sz.size()], "|", 1);
            memmove(&message->content[messageType.size() + 1 + sz.size() + 1], messageContent.c_str(), messageContent.size());

            message->n = strlen(message->content);
            message->i = 0;

            messageQueue.push(message);

            notify();
        }

        /**
         * Alerts the thread that there is pending data
         */
        void notify() {
            if (!hasPendingData.test()) {
                std::lock_guard lock{mQueue};
                hasPendingData.test_and_set();
                cvQueue.notify_one();
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
                    std::unique_lock uniqueLock{mQueue};
                    cvQueue.wait(uniqueLock, [this]() { return hasPendingData.test(); });

                    uniqueLock.unlock();

                    bool isPushing{};
                    Message* message;
                    while (messageQueue.try_pop(message)) {
                        // if (++count == 1) {
                        //     auto t = std::chrono::high_resolution_clock::now().time_since_epoch();
                        //     auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t);
                        //     printf("Message popped (%s): %zu\n", message->messageType.c_str(), ms.count());
                        //     count = 0;
                        // }

                        for (auto* subscriber : subscribers) {
                            if (subscriber->isSubscribed(message->messageType)) {
                                subscriber->push(&ring, message);
                                isPushing = true;
                            }
                        }
                    }

                    {
                        std::lock_guard lockGuard{mQueue};
                        if (messageQueue.empty()) {
                            hasPendingData.clear();
                        }
                    }

                    if (!isPushing) {
                        goto outer;
                    }

                    while (isRunning.test()) {
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

                        if (std::all_of(subscribers.begin(), subscribers.end(), [](MessageSubscriber* o) {
                            return o->isIdle();
                        })) {
                            goto outer;
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
            subscribers.emplace_back(subscriber);
        }
    };

    inline PushService _mq{};

    static PushService& getPushService() {
        return gazellemq::server::_mq;
    }
}

#endif //GAZELLEMQ_SERVER_PUSHSERVICE_HPP
