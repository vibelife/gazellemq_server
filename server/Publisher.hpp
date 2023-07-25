#ifndef GAZELLEMQ_PUBLISHER_HPP
#define GAZELLEMQ_PUBLISHER_HPP

#include "ClientConnection.hpp"

namespace gazellemq::server {
    class Publisher : public ClientConnection {
    public:
        void handleEvent(struct io_uring *ring) override {

        }
    };
}

#endif //GAZELLEMQ_PUBLISHER_HPP
