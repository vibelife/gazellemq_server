#ifndef GAZELLEMQ_MESSAGESUBSCRIBER_HPP
#define GAZELLEMQ_MESSAGESUBSCRIBER_HPP

#include <list>
#include "MessageHandler.hpp"
#include "Consts.hpp"
#include "StringUtils.hpp"
#include "PushService.hpp"
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
            return (currentChunk.n == 0) && pendingChunks.empty();
        }

        /**
         * Sends the data to the subscriber, or queues it to be sent later.
         * @param ring
         * @param chunk
         */
        void push(struct io_uring *ring, MessageChunk const& chunk) {
            if (currentChunk.n == 0) {
                currentChunk = chunk;
                sendCurrentChunk(ring);
            } else if (currentChunk.n > currentChunk.i) {
                pendingChunks.emplace_back(chunk);
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
                    // To get here means we've sent all the data

                    // go to the next chunk if one exists
                    if (!pendingChunks.empty()) {
                        currentChunk = std::move(pendingChunks.front());
                        pendingChunks.pop_front();
                        sendCurrentChunk(ring);
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
