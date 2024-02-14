#ifndef GAZELLEMQ_SERVER_MESSAGEQUEUE_HPP
#define GAZELLEMQ_SERVER_MESSAGEQUEUE_HPP

#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "Message.hpp"

namespace gazellemq::server {
    class MessageQueue {
    private:
        rigtorp::MPMCQueue<MessageBatch> messageQueue;
    public:
        std::atomic_flag afQueue{false};
    public:
        explicit MessageQueue(size_t messageQueueDepth)
            :messageQueue(messageQueueDepth)
        {}
    public:
        void push_back(MessageBatch &&chunk) {
            messageQueue.push(std::move(chunk));
            afQueue.test_and_set();
            afQueue.notify_one();
        }

        bool try_pop(MessageBatch& chunk) {
            return messageQueue.try_pop(chunk);
        }
    };

    static inline MessageQueue _mcq{1000000};

    static MessageQueue& getMessageChunkQueue() {
        return gazellemq::server::_mcq;
    }
}

#endif //GAZELLEMQ_SERVER_MESSAGEQUEUE_HPP
