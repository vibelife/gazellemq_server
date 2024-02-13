#ifndef GAZELLEMQ_SERVER_MESSAGECHUNKQUEUE_HPP
#define GAZELLEMQ_SERVER_MESSAGECHUNKQUEUE_HPP

#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "MessageChunk.hpp"

namespace gazellemq::server {
    class MessageChunkQueue {
    private:
        rigtorp::MPMCQueue<MessageChunk> messageQueue;
    public:
        std::atomic_flag afQueue{false};
    public:
        explicit MessageChunkQueue(size_t messageQueueDepth)
            :messageQueue(messageQueueDepth)
        {}
    public:
        void push_back(MessageChunk &&chunk) {
            messageQueue.push(std::move(chunk));
            afQueue.test_and_set();
            afQueue.notify_one();
        }

        bool try_pop(MessageChunk& chunk) {
            return messageQueue.try_pop(chunk);
        }
    };

    static inline MessageChunkQueue _mcq{500000};

    static MessageChunkQueue& getMessageChunkQueue() {
        return gazellemq::server::_mcq;
    }
}

#endif //GAZELLEMQ_SERVER_MESSAGECHUNKQUEUE_HPP
