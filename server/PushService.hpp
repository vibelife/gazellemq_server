#ifndef GAZELLEMQ_SERVER_PUSHSERVICE_HPP
#define GAZELLEMQ_SERVER_PUSHSERVICE_HPP

#include <condition_variable>
#include <thread>
#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "DataPush.hpp"
#include "MessageSubscriber.hpp"


namespace gazellemq::server {
    class PushService {
    private:
        rigtorp::MPMCQueue<DataPush> queue;
        std::vector<MessageSubscriber*> subscribers;

        std::mutex mQueue;
        std::condition_variable cvQueue;
        std::atomic_flag hasPendingData{false};
        std::atomic_flag isRunning{true};
        std::jthread bgThread;

    public:
        explicit PushService(size_t queueDepth)
            :queue(queueDepth)
        {}

    public:
        /**
         * Pushes a message on to the queue. Subscribers of the message will receive it asynchronously.
         * @param messageType
         * @param messageContent
         */
        void push(std::string&& messageType, std::string&& messageContent) {
            std::string msg;
            msg.reserve(messageType.size() + messageContent.size() + 16);

            msg.append(messageType);
            msg.push_back('|');
            msg.append(std::to_string(messageContent.size()));
            msg.push_back('|');
            msg.append(messageContent);

            bool hasSubscribers{false};
            for (auto* subscriber : subscribers) {
                if (subscriber->isSubscribed(messageType)) {
                    hasSubscribers = true;
                    DataPush dataPush;
                    dataPush.message.reserve(msg.size());
                    dataPush.message.swap(msg);
                    queue.push(std::move(dataPush));
                }
            }



            if (hasSubscribers) {
                notify();
            }
        }

        /**
         * Registers a subscriber so it can receive messages
         * @param subscriber
         */
        void registerSubscriber(MessageSubscriber* subscriber) {
            subscribers.emplace_back(subscriber);
        }

        /**
         * Starts watching the work queue
         */
        void start() {
            using namespace std::chrono_literals;

            bgThread = std::jthread{[this]() {
                while (isRunning.test()) {
                    std::unique_lock uniqueLock{mQueue};
                    cvQueue.wait(uniqueLock, [this]() { return hasPendingData.test(); });

                    uniqueLock.unlock();

                    while (!queue.empty()) {
                        DataPush dataPush;
                        if (queue.try_pop(dataPush)) {
                            pushMessage(std::move(dataPush));
                        }
                    }

                    {
                        std::lock_guard lk(mQueue);
                        hasPendingData.clear();
                    }
                }
            }};
        }

    private:
        /**
         * Pushes the dataPush to the subscriber
         * @param dataPush
         */
        void pushMessage(DataPush&& dataPush) {

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
    };

    inline PushService _mq{4096};

    static PushService& getPushService() {
        return gazellemq::server::_mq;
    }
}

#endif //GAZELLEMQ_SERVER_PUSHSERVICE_HPP
