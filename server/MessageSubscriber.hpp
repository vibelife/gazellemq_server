#ifndef GAZELLEMQ_MESSAGESUBSCRIBER_HPP
#define GAZELLEMQ_MESSAGESUBSCRIBER_HPP

#include <list>
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
            MessageSubscriberState_ready,
            DataPushState_sendData,
        };

        MessageSubscriberState state{MessageSubscriberState_notSet};
        std::vector<std::string> subscriptions;
        char readBuffer[MAX_READ_BUF]{};
        std::string subscriptionsBuffer;
        std::list<Message*> pendingMessages;
        Message* currentMessage{};
        size_t count{};
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
                state = MessageSubscriberState_ready;
            } else {
                beginReceiveSubscriptions(ring);
            }
        }

        /**
         * Sends an acknowledgement to the client
         * @param ring
         */
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

        [[nodiscard]] bool isIdle() const {
            return (currentMessage == nullptr || currentMessage->content.empty()) && pendingMessages.empty();
        }

        /**
         * Sends the data pushToSubscribers, or queues it to be sent later.
         * @param ring
         * @param message
         */
        void push(struct io_uring *ring, Message* message) {
            if (currentMessage == nullptr) {
                currentMessage = message;
                sendData(ring);
            } else if (!currentMessage->content.empty()) {
                pendingMessages.emplace_back(message);
            } else {
                // we should never get here
                pendingMessages.emplace_back(message);
                delete currentMessage;
                currentMessage = pendingMessages.front();
                pendingMessages.pop_front();
                sendData(ring);
            }
        }

        /**
         * Sends the remaining bytes to the endpoint
         * @param ring
         */
        void sendData(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, currentMessage->content.c_str(), currentMessage->content.size(), 0);

            state = DataPushState_sendData;
            io_uring_sqe_set_data(sqe, this);
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done sending the bytes
         * @param ring
         * @param res
         * @return
         */
        void onSendDataComplete(struct io_uring *ring, int res) {
            if (res > -1) {
                currentMessage->content.erase(0, res);
                if (currentMessage->content.empty()) {
                    // if (++count == 50000) {
                    //     auto t = std::chrono::high_resolution_clock::now().time_since_epoch();
                    //     auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t);
                    //     printf("(2) Done sending: %zu\n", ms.count());
                    //     count = 0;
                    // }

                    // To get here means we've sent all the data
                    delete currentMessage;
                    currentMessage = nullptr;

                    // go to the next pushToSubscribers if one exists
                    if (!pendingMessages.empty()) {
                        currentMessage = pendingMessages.front();
                        pendingMessages.pop_front();
                        sendData(ring);
                    } else {
                        state = MessageSubscriberState_ready;

                    }
                } else {
                    sendData(ring);
                }
            } else {
                printError("onSendDataComplete", res);
            }
        }

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
                case DataPushState_sendData:
                    onSendDataComplete(ring, res);
                    break;
                default:
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_MESSAGESUBSCRIBER_HPP
