#ifndef GAZELLEMQ_SUBSCRIBER_HPP
#define GAZELLEMQ_SUBSCRIBER_HPP

#include "ClientConnection.hpp"

namespace gazellemq::server {
    class Subscriber : public ClientConnection {
    public:
        void handleEvent(struct io_uring *ring) override {

        }
    };
}

#endif //GAZELLEMQ_SUBSCRIBER_HPP
