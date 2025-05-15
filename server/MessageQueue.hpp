#ifndef GAZELLEMQ_SERVER_MESSAGEQUEUE_HPP
#define GAZELLEMQ_SERVER_MESSAGEQUEUE_HPP

#include <condition_variable>

#include "MessageBatch.hpp"
#include "../lib/MPMCQueue/MPMCQueue.hpp"

namespace gazellemq::server {
    class MessageQueue {
    private:
        rigtorp::MPMCQueue<MessageBatch> messageQueue;
    public:
        std::atomic_flag afQueue{false};

        std::mutex mQueue;
        std::condition_variable cvQueue{};
        std::atomic_flag hasPendingData{false};
    public:
        explicit MessageQueue(const size_t messageQueueDepth)
            :messageQueue(messageQueueDepth)
        {}
    public:
        void push_back(MessageBatch &&chunk) {
            messageQueue.push(std::move(chunk));
            afQueue.test_and_set();
            afQueue.notify_one();

            {
                std::lock_guard lock{mQueue};
                if (!hasPendingData.test()) {
                    hasPendingData.test_and_set();
                    cvQueue.notify_all();
                }
            }
        }

        bool try_pop(MessageBatch& chunk) {
            return messageQueue.try_pop(chunk);
        }
    };

    static inline MessageQueue _mcq{1000000};

    static MessageQueue& getMessageQueue() {
        return gazellemq::server::_mcq;
    }
}

#endif //GAZELLEMQ_SERVER_MESSAGEQUEUE_HPP
