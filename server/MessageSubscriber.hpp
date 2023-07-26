#ifndef GAZELLEMQ_MESSAGESUBSCRIBER_HPP
#define GAZELLEMQ_MESSAGESUBSCRIBER_HPP

#include "MessageHandler.hpp"

namespace gazellemq::server {
    class MessageSubscriber : public MessageHandler {
    public:
        virtual ~MessageSubscriber() = default;
        void handleEvent(struct io_uring *ring, int res) override {

        }
    };
}

#endif //GAZELLEMQ_MESSAGESUBSCRIBER_HPP
