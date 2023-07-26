#ifndef GAZELLEMQ_MESSAGEPUBLISHER_HPP
#define GAZELLEMQ_MESSAGEPUBLISHER_HPP

#include "MessageHandler.hpp"

namespace gazellemq::server {
    class MessagePublisher : public MessageHandler {
    public:
        void handleEvent(struct io_uring *ring, int res) override {

        }
    };
}

#endif //GAZELLEMQ_MESSAGEPUBLISHER_HPP
