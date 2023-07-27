#ifndef GAZELLEMQ_MESSAGEPUBLISHER_HPP
#define GAZELLEMQ_MESSAGEPUBLISHER_HPP

#include "MessageHandler.hpp"
#include "MessageQueue.hpp"

#include "Consts.hpp"

namespace gazellemq::server {
    class MessagePublisher : public MessageHandler {
    private:
        enum MessagePublisherState {
            MessagePublisherState_notSet,
            MessagePublisherState_receiveData,
        };

        char readBuffer[MAX_READ_BUF]{};
        std::string content;

        MessagePublisherState state{MessagePublisherState_notSet};

    private:
        /**
         * Receives data from the publisher
         * @param ring
         */
        void beginReceiveData(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            state = MessagePublisherState_receiveData;
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done receiving data
         * @param ring
         * @param res
         */
        void onReceiveDataComplete(struct io_uring *ring, int res) {
            content.append(readBuffer, res);
        }
    public:
        explicit MessagePublisher(int fileDescriptor)
            :MessageHandler(fileDescriptor)
        {}

        ~MessagePublisher() override = default;

        void printHello() override {
            printf("Publisher connected - %s\n", clientName.c_str());
        }

        /**
         * Does the appropriate action based on the current state
         * @param ring
         * @param res
         */
        void handleEvent(struct io_uring *ring, int res) override {
            switch (state) {
                case MessagePublisherState_notSet:
                    beginReceiveData(ring);
                    break;
                case MessagePublisherState_receiveData:
                    onReceiveDataComplete(ring, res);
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_MESSAGEPUBLISHER_HPP
