#ifndef GAZELLEMQ_MESSAGESUBSCRIBER_HPP
#define GAZELLEMQ_MESSAGESUBSCRIBER_HPP

#include "MessageHandler.hpp"
#include "Consts.hpp"
#include "StringUtils.hpp"

namespace gazellemq::server {
    class MessageSubscriber : public MessageHandler {
    private:
        enum MessageSubscriberState {
            MessageSubscriberState_notSet,
            MessageSubscriberState_receiveSubscriptions,
            MessageSubscriberState_sendAck,
            MessageSubscriberState_listening,
        };

        MessageSubscriberState state{MessageSubscriberState_notSet};
        std::vector<std::string> subscriptions;
        char readBuffer[MAX_READ_BUF]{};
        std::string subscriptionsBuffer;
    private:
        /**
         * Receives subscriptions from the subscriber
         * @param ring
         */
        void beginReceiveSubscriptions(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            state = MessageSubscriberState_receiveSubscriptions;
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done receiving data
         * @param ring
         * @param res
         */
        void onReceiveSubscriptionsComplete(struct io_uring *ring, int res) {
            subscriptionsBuffer.append(readBuffer, res);
            if (subscriptionsBuffer.ends_with('\r')) {
                subscriptionsBuffer.erase(subscriptionsBuffer.size() - 1, 1);
                gazellemq::utils::split(std::move(subscriptionsBuffer), subscriptions);
                state = MessageSubscriberState_listening;
            } else {
                beginReceiveSubscriptions(ring);
            }
        }

        void beginSendAck(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, "\r", 1, 0);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            state = MessageSubscriberState_sendAck;
            io_uring_submit(ring);
        }

        void onSendAckComplete(struct io_uring *ring, int res) {
            beginReceiveSubscriptions(ring);
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
                    beginSendAck(ring);
                    break;
                case MessageSubscriberState_sendAck:
                    onSendAckComplete(ring, res);
                    break;
                case MessageSubscriberState_receiveSubscriptions:
                    onReceiveSubscriptionsComplete(ring, res);
                    break;
                default:
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_MESSAGESUBSCRIBER_HPP
