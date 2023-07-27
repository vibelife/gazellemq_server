#ifndef GAZELLEMQ_MESSAGESUBSCRIBER_HPP
#define GAZELLEMQ_MESSAGESUBSCRIBER_HPP

#include "MessageHandler.hpp"

namespace gazellemq::server {
    class MessageSubscriber : public MessageHandler {
    public:
        explicit MessageSubscriber(int fileDescriptor)
                :MessageHandler(fileDescriptor)
        {}

        ~MessageSubscriber() override = default;

        void printHello() override {
            printf("Subscriber connected - %s\n", clientName.c_str());
        }

        void handleEvent(struct io_uring *ring, int res) override {

        }
    };
}

#endif //GAZELLEMQ_MESSAGESUBSCRIBER_HPP
