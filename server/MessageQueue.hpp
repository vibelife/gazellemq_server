#ifndef GAZELLEMQ_SERVER_MESSAGEQUEUE_HPP
#define GAZELLEMQ_SERVER_MESSAGEQUEUE_HPP

#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "Message.hpp"

namespace gazellemq::server {
    class MessageQueue {
    private:
        rigtorp::MPMCQueue<Message> queue;
    public:
        explicit MessageQueue(size_t queueDepth)
            :queue(queueDepth)
        {}

    public:
        void push(Message&& msg) {
            queue.push(std::move(msg));
        }
    };

    inline MessageQueue _mq{4096};

    static MessageQueue& getQueue() {
        return gazellemq::server::_mq;
    }
}

#endif //GAZELLEMQ_SERVER_MESSAGEQUEUE_HPP
