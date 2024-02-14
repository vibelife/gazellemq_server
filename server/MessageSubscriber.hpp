#ifndef GAZELLEMQ_MESSAGESUBSCRIBER_HPP
#define GAZELLEMQ_MESSAGESUBSCRIBER_HPP

#include <list>
#include "MessageHandler.hpp"
#include "Consts.hpp"
#include "StringUtils.hpp"
#include "Message.hpp"

namespace gazellemq::server {
    class MessageSubscriber : public MessageHandler {
    private:
        enum MessageSubscriberState {
            MessageSubscriberState_notSet,
            MessageSubscriberState_receiveSubscriptions,
            MessageSubscriberState_sendAck,
            MessageSubscriberState_ready,
            MessageSubscriberState_sendData,
            MessageSubscriberState_disconnect,
            MessageSubscriberState_zombie,
        };

        MessageSubscriberState state{MessageSubscriberState_notSet};
        std::vector<std::string> subscriptions;
        char readBuffer[MAX_READ_BUF]{};
        std::string subscriptionsBuffer;
        std::list<Message> pendingMessages;
        Message currentMessage{};
    private:
        /**
         * Receives subscriptions from the subscriber
         * @param ring
         */
        void beginReceiveSubscriptions(struct io_uring *ring) {
            memset(readBuffer, 0, MAX_READ_BUF);
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, this);

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
            io_uring_sqe_set_data(sqe, this);

            state = MessageSubscriberState_sendAck;
            io_uring_submit(ring);
        }

        void onSendAckComplete(struct io_uring *ring, int res) {
            beginReceiveSubscriptions(ring);
        }
    public:
        explicit MessageSubscriber(
                int fileDescriptor,
                std::string&& name
            )
                :MessageHandler(fileDescriptor, std::move(name))
        {}

        MessageSubscriber(MessageSubscriber &&other) noexcept: MessageHandler(other.fd) {
            std::swap(this->state, other.state);
            std::swap(this->subscriptions, other.subscriptions);
            std::swap(this->readBuffer, other.readBuffer);
            std::swap(this->subscriptionsBuffer, other.subscriptionsBuffer);
            std::swap(this->pendingMessages, other.pendingMessages);
            std::swap(this->currentMessage, other.currentMessage);

            std::swap(this->clientName, other.clientName);
            std::swap(this->fd, other.fd);
            std::swap(this->isZombie, other.isZombie);
            std::swap(this->isDisconnecting, other.isDisconnecting);
        }

        MessageSubscriber& operator=(MessageSubscriber &&other) noexcept {
            std::swap(this->state, other.state);
            std::swap(this->subscriptions, other.subscriptions);
            std::swap(this->readBuffer, other.readBuffer);
            std::swap(this->subscriptionsBuffer, other.subscriptionsBuffer);
            std::swap(this->pendingMessages, other.pendingMessages);
            std::swap(this->currentMessage, other.currentMessage);

            std::swap(this->clientName, other.clientName);
            std::swap(this->fd, other.fd);
            std::swap(this->isZombie, other.isZombie);
            std::swap(this->isDisconnecting, other.isDisconnecting);

            return *this;
        }

        MessageSubscriber(MessageSubscriber const& other) = default;
        MessageSubscriber& operator=(MessageSubscriber const& other) = delete;

        ~MessageSubscriber() override = default;

        /**
         * Returns true
         * @return
         */
        [[nodiscard]] bool isSubscriber() const override {
            return true;
        }

        /**
         * Disconnects from the server
         * @param ring
         */
        virtual void beginDisconnect(struct io_uring* ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_close(sqe, fd);
            state = MessageSubscriberState_disconnect;
            io_uring_sqe_set_data(sqe, this);

            io_uring_submit(ring);
        }

        void onDisconnected(struct io_uring *ring, int res) {
            printf("A subscriber disconnected [%s]\n", clientName.c_str());
            state = MessageSubscriberState_zombie;
            markForRemoval();
        }

        /**
         * Returns true if this subscriber is not transferring data.
         * @return
         */
        [[nodiscard]] bool isIdle() const {
            return currentMessage.isDone() && pendingMessages.empty() && (state == MessageSubscriberState_ready || state == MessageSubscriberState_zombie);
        }

        /**
         * Sends the data to the subscriber, or queues it to be sent later.
         * @param ring
         * @param chunk
         */
        void pushMessage(struct io_uring *ring, Message const& chunk) {
            if (!currentMessage.hasContent()) {
                currentMessage = chunk;
                sendCurrentMessage(ring);
            } else {
                pendingMessages.push_back(chunk);
            }
        }

        void sendNextPendingMessage(struct io_uring *ring) {
            currentMessage = std::move(pendingMessages.front());
            pendingMessages.pop_front();

            if (currentMessage.hasContent()) {
                currentMessage.setBusy();
                sendCurrentMessage(ring);
            }
        }

        /**
         * Sends the remaining bytes to the endpoint
         * @param ring
         */
        void sendCurrentMessage(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, currentMessage.getContentRemaining(), currentMessage.getContentRemainingLength(), 0);

            state = MessageSubscriberState_sendData;
            io_uring_sqe_set_data(sqe, this);
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done sending the bytes
         * @param ring
         * @param res
         * @return
         */
        void onSendCurrentMessageComplete(struct io_uring *ring, int res) {
            if (res > -1) {
                currentMessage.advance(res);
                if (currentMessage.isDone()) {
                    currentMessage.clear();
                    // To get here means we've sent all the data

                    // collect the next chunks
                    if (!pendingMessages.empty()) {
                        sendNextPendingMessage(ring);
                    } else {
                        state = MessageSubscriberState_ready;
                    }
                } else {
                    sendCurrentMessage(ring);
                }
            } else {
                printError("onSendCurrentMessageComplete", res);
                beginDisconnect(ring);
            }
        }

        /**
         * Returns true if the passed in string is subscribed to by this subscriber.
         * @param messageType
         * @return
         */
        [[nodiscard]] bool isSubscribed(std::string_view messageType) const {
            if (isZombie) {
                return false;
            }

            return std::any_of(subscriptions.begin(), subscriptions.end(), [messageType](std::string const& o) {
                return o == messageType;
            });
        }


        void printHello() const override {
            printf("Subscriber connected - %s\n", clientName.c_str());
        }

        /**
         * Does the appropriate action based on the current state
         * @param ring
         * @param res
         */
        void handleEvent(struct io_uring *ring, int res) override {
            if (isDisconnecting) {
                onDisconnected(ring, res);
                return;
            }

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
                case MessageSubscriberState_sendData:
                    onSendCurrentMessageComplete(ring, res);
                    break;
                case MessageSubscriberState_disconnect:
                    onDisconnected(ring, res);
                    break;
                default:
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_MESSAGESUBSCRIBER_HPP
