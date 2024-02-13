#ifndef GAZELLEMQ_MESSAGESUBSCRIBER_HPP
#define GAZELLEMQ_MESSAGESUBSCRIBER_HPP

#include <list>
#include "MessageHandler.hpp"
#include "Consts.hpp"
#include "StringUtils.hpp"
#include "MessageChunk.hpp"

namespace gazellemq::server {
    class MessageSubscriber : public MessageHandler {
    private:
        enum MessageSubscriberState {
            MessageSubscriberState_notSet,
            MessageSubscriberState_receiveSubscriptions,
            MessageSubscriberState_sendAck,
            MessageSubscriberState_ready,
            MessageSubscriberState_sendData
        };

        MessageSubscriberState state{MessageSubscriberState_notSet};
        std::vector<std::string> subscriptions;
        char readBuffer[MAX_READ_BUF]{};
        std::string subscriptionsBuffer;
        std::list<MessageChunk> pendingChunks;
        MessageChunk currentChunk{};
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
            std::swap(this->pendingChunks, other.pendingChunks);
            std::swap(this->currentChunk, other.currentChunk);

            std::swap(this->clientName, other.clientName);
            std::swap(this->fd, other.fd);
            std::swap(this->isZombie, other.isZombie);
            std::swap(this->mustDisconnect, other.mustDisconnect);
        }

        MessageSubscriber& operator=(MessageSubscriber &&other) noexcept {
            std::swap(this->state, other.state);
            std::swap(this->subscriptions, other.subscriptions);
            std::swap(this->readBuffer, other.readBuffer);
            std::swap(this->subscriptionsBuffer, other.subscriptionsBuffer);
            std::swap(this->pendingChunks, other.pendingChunks);
            std::swap(this->currentChunk, other.currentChunk);

            std::swap(this->clientName, other.clientName);
            std::swap(this->fd, other.fd);
            std::swap(this->isZombie, other.isZombie);
            std::swap(this->mustDisconnect, other.mustDisconnect);

            return *this;
        }

        MessageSubscriber(MessageSubscriber const& other) = default;
        MessageSubscriber& operator=(MessageSubscriber const& other) = delete;

        ~MessageSubscriber() override = default;

        /**
         * Returns true
         * @return
         */
        [[nodiscard]] virtual bool isSubscriber() const override {
            return true;
        }

        /**
         * Returns true if this subscriber is not transferring data.
         * @return
         */
        [[nodiscard]] bool isIdle() const {
            return (currentChunk.n == 0) && pendingChunks.empty() && state == MessageSubscriberState_ready;
        }

        /**
         * Sends the data to the subscriber, or queues it to be sent later.
         * @param ring
         * @param chunk
         */
        void push(MessageChunk const& chunk) {
            if (!currentChunk.tryAppend(chunk)) {
                pendingChunks.push_back(chunk);
            }
        }

        void send(struct io_uring *ring) {
            if ((!currentChunk.isBusy) && currentChunk.i == 0 && currentChunk.n > 0) {
                currentChunk.isBusy = true;
                sendCurrentChunk(ring);
            }
        }

        void sendNextPendingChunks(struct io_uring *ring) {
            std::for_each(pendingChunks.begin(), pendingChunks.end(), [this](MessageChunk& chunk) {
                currentChunk.tryTake(chunk);
            });
            pendingChunks.erase(
                    std::remove_if(pendingChunks.begin(), pendingChunks.end(), [](auto& o) { return o.getCanRemove();}),
                    pendingChunks.end()
            );

            if (!currentChunk.content.empty()) {
                currentChunk.isBusy = true;
                sendCurrentChunk(ring);
            }
        }

        /**
         * Sends the remaining bytes to the endpoint
         * @param ring
         */
        void sendCurrentChunk(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, &currentChunk.content[currentChunk.i], currentChunk.n, 0);

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
        void onSendCurrentChunkComplete(struct io_uring *ring, int res) {
            if (res > -1) {
                currentChunk.i += res;
                currentChunk.n -= res;
                if (currentChunk.n == 0) {
                    currentChunk.clear();
                    // To get here means we've sent all the data

                    // collect the next chunks
                    if (!pendingChunks.empty()) {
                        sendNextPendingChunks(ring);
                    } else {
                        state = MessageSubscriberState_ready;
                    }
                } else {
                    sendCurrentChunk(ring);
                }
            } else {
                printError("onSendCurrentChunkComplete", res);
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
                case MessageSubscriberState_sendData:
                    onSendCurrentChunkComplete(ring, res);
                    break;
                default:
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_MESSAGESUBSCRIBER_HPP
