#ifndef GAZELLEMQ_MESSAGESUBSCRIBER_HPP
#define GAZELLEMQ_MESSAGESUBSCRIBER_HPP

#include "MessageHandler.hpp"

namespace gazellemq::server {
    class MessageSubscriber : public MessageHandler {
    private:
        enum MessageSubscriberState {
            MessageSubscriberState_notSet,
        };

        MessageSubscriberState state{MessageSubscriberState_notSet};
        std::vector<std::string> subscriptions;
    private:
        void init() const {

        }
    public:
        explicit MessageSubscriber(int fileDescriptor)
                :MessageHandler(fileDescriptor)
        {}

        ~MessageSubscriber() override = default;

        /**
         * Returns true if the passed in string is subscribed to by this subscriber.
         * @param messageType
         * @return
         */
        [[nodiscard]] bool isSubscribed(std::string_view messageType) const {
            return std::any_of(subscriptions.begin(), subscriptions.end(), [messageType](std::string const& o) {
                return o == messageType;
            });
        }


        void printHello() const override {
            printf("Subscriber connected - %s\n", clientName.c_str());
        }

        void handleEvent(struct io_uring *ring, int res) override {
            switch (state) {
                case MessageSubscriberState_notSet:
                    init();
                    break;
                default:
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_MESSAGESUBSCRIBER_HPP
