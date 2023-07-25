#ifndef GAZELLEMQ_CLIENTCONNECTION_HPP
#define GAZELLEMQ_CLIENTCONNECTION_HPP

#include <liburing.h>
#include "EventLoopObject.hpp"
#include "Consts.hpp"

namespace gazellemq::server {
    class ClientConnection: public EventLoopObject {
    private:
        char readBuffer[MAX_READ_BUF]{};
    public:
        int fd{};
    public:
        void handleEvent(struct io_uring *ring) override {

        }

        void beginDisconnect(struct io_uring* ring) {

        }

        /**
         * Wait for the client to send indicate whether it is a publisher or subscriber
         * @param client
         */
        void beginReceiveIntent(struct io_uring* ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            event = Enums::Event::Event_ReceiveIntent;
            io_uring_submit(ring);
        }
    };
}

#endif //GAZELLEMQ_CLIENTCONNECTION_HPP
