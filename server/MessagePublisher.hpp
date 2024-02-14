#ifndef GAZELLEMQ_MESSAGEPUBLISHER_HPP
#define GAZELLEMQ_MESSAGEPUBLISHER_HPP

#include <functional>
#include "MessageHandler.hpp"
#include "Consts.hpp"
#include "MessageBatch.hpp"

namespace gazellemq::server {
    class MessagePublisher : public MessageHandler {
    private:
        enum MessagePublisherState {
            MessagePublisherState_notSet,
            MessagePublisherState_receiveData,
            MessagePublisherState_disconnect,
            MessagePublisherState_sendAck,
            MessagePublisherState_zombie,
        };

        char readBuffer[MAX_READ_BUF]{};

        MessagePublisherState state{};

        enum ParseState {
            ParseState_messageType,
            ParseState_messageContentLength,
            ParseState_messageContent,
        };

        ParseState parseState{};

        std::string messageType;
        std::string messageLengthBuffer;
        std::string messageContent;
        size_t messageContentLength{};
        size_t nbContentBytesRead{};

        MessageBatch currentBatch{};
        MessageBatch nextBatch{};

        std::function<void(MessageBatch&& batch)> fnPushToQueue;
    private:
        /**
         * Disconnects from the server
         * @param ring
         */
        virtual void beginDisconnect(struct io_uring* ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_close(sqe, fd);
            state = MessagePublisherState_disconnect;
            io_uring_sqe_set_data(sqe, this);

            io_uring_submit(ring);
        }

        void onDisconnected(struct io_uring *ring, int res) {
            printf("A publisher disconnected [%s]\n", clientName.c_str());
            state = MessagePublisherState_zombie;
            markForRemoval();
        }

        /**
         * Sends an acknowledgement to the client
         * @param ring
         */
        void beginSendAck(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, "\r", 1, 0);
            io_uring_sqe_set_data(sqe, this);

            state = MessagePublisherState_sendAck;
            io_uring_submit(ring);
        }

        void onSendAckComplete(struct io_uring *ring, int res) {
            beginReceiveData(ring);
        }

        /**
         * Receives data from the publisher
         * @param ring
         */
        void beginReceiveData(struct io_uring *ring) {
            memset(readBuffer, 0, MAX_READ_BUF);
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, this);

            state = MessagePublisherState_receiveData;
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done receiving data
         * @param ring
         * @param res
         */
        void onReceiveDataComplete(struct io_uring *ring, int res) {
            if (res <= 0) {
                // The client has disconnected
                beginDisconnect(ring);
            } else if (!isDisconnecting && !isZombie) {
                forwardMessage(readBuffer, res);

                beginReceiveData(ring);
            }
        }

        /**
         * forwards the message to subscribers
         * @param buffer
         * @param bufferLength
         */
        void forwardMessage(char const* buffer, size_t bufferLength) {
            for (size_t i{0}; i < bufferLength; ++i) {
                char ch {buffer[i]};
                if (parseState == ParseState_messageType) {
                    if (ch == '|') {
                        parseState = ParseState_messageContentLength;
                        continue;
                    } else {
                        messageType.push_back(ch);
                    }
                } else if (parseState == ParseState_messageContentLength) {
                    if (ch == '|') {
                        messageContentLength = std::stoul(messageLengthBuffer);
                        parseState = ParseState_messageContent;
                        continue;
                    } else {
                        messageLengthBuffer.push_back(ch);
                    }
                } else if (parseState == ParseState_messageContent) {
                    size_t nbCharsNeeded {messageContentLength - nbContentBytesRead};
                    // add as many characters as possible in bulk

                    if ((i + nbCharsNeeded) <= bufferLength) {
                        messageContent.append(&buffer[i], nbCharsNeeded);
                        // do nothing here for now
                    } else {
                        nbCharsNeeded = bufferLength - i;
                        messageContent.append(&buffer[i], nbCharsNeeded);
                    }

                    nbContentBytesRead += nbCharsNeeded;
                    i += nbCharsNeeded - 1;

                    if (messageContentLength == nbContentBytesRead) {
                        // Done parsing
                        std::string message{};
                        message.append(messageType);
                        message.push_back('|');
                        message.append(messageLengthBuffer);
                        message.push_back('|');
                        message.append(messageContent);

                        if (currentBatch.getMessageType().empty()) {
                            currentBatch.setMessageType(messageType);
                        } else if ((currentBatch.getMessageType() != messageType) || (currentBatch.isFull())) {
                            fnPushToQueue(std::move(currentBatch));
                            currentBatch.clearForNextMessage();
                            currentBatch.setMessageType(messageType);
                        }

                        currentBatch.append(std::move(message));

                        if (i == (bufferLength-1)) {
                            fnPushToQueue(std::move(currentBatch));
                            currentBatch.clearForNextMessage();
                        }


                        // fnPushToQueue(messageType, std::move(message));
                        messageContentLength = 0;
                        nbContentBytesRead = 0;
                        messageLengthBuffer.clear();
                        messageType.clear();
                        messageContent.clear();
                        parseState = ParseState_messageType;
                    }
                }
            }
        }

        void swap(MessagePublisher &&other) {
            std::swap(this->readBuffer, other.readBuffer);
            std::swap(this->state, other.state);
            std::swap(this->parseState, other.parseState);
            std::swap(this->messageType, other.messageType);
            std::swap(this->messageLengthBuffer, other.messageLengthBuffer);
            std::swap(this->messageContentLength, other.messageContentLength);
            std::swap(this->nbContentBytesRead, other.nbContentBytesRead);
            //std::swap(this->fnPushToQueue, other.fnPushToQueue);
            std::swap(this->fnPushToQueue, other.fnPushToQueue);
            std::swap(this->currentBatch, other.currentBatch);
            std::swap(this->nextBatch, other.nextBatch);

            std::swap(this->clientName, other.clientName);
            std::swap(this->fd, other.fd);
            std::swap(this->isZombie, other.isZombie);
            std::swap(this->isDisconnecting, other.isDisconnecting);
        }
    public:
        explicit MessagePublisher(
                int fileDescriptor,
                std::string&& name,
                std::function<void(MessageBatch&& buffer)>&& fnPushToQueue
            )
            : MessageHandler(fileDescriptor, std::move(name)), fnPushToQueue(std::move(fnPushToQueue))
        {}

        MessagePublisher(MessagePublisher &&other) noexcept: MessageHandler(other.fd) {
            this->swap(std::move(other));
        }

        MessagePublisher& operator=(MessagePublisher &&other) noexcept {
            this->swap(std::move(other));
            return *this;
        }

        MessagePublisher(MessagePublisher const& other) = delete;
        MessagePublisher& operator=(MessagePublisher const& other) = delete;

        ~MessagePublisher() override = default;

        void printHello() const override {
            printf("Publisher connected - %s\n", clientName.c_str());
        }

        void disconnect(struct io_uring *ring) {
            beginDisconnect(ring);
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
                case MessagePublisherState_notSet:
                    beginSendAck(ring);
                    break;
                case MessagePublisherState_sendAck:
                    onSendAckComplete(ring, res);
                    break;
                case MessagePublisherState_receiveData:
                    onReceiveDataComplete(ring, res);
                    break;
                case MessagePublisherState_disconnect:
                    onDisconnected(ring, res);
                    break;
                default:
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_MESSAGEPUBLISHER_HPP
