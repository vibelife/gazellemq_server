#ifndef PUBLISHERHANDLER_HPP
#define PUBLISHERHANDLER_HPP

#include <condition_variable>

#include "../MessageBatch.hpp"
#include "../MessageQueue.hpp"
#include "../../lib/MPMCQueue/MPMCQueue.hpp"
#include "../PubSubHandler.hpp"

namespace gazellemq::server {
    class TCPPublisherHandler final : public PubSubHandler {
    private:
        enum ParseState {
            ParseState_messageType,
            ParseState_messageContentLength,
            ParseState_messageContent,
        };

        std::string writeBuffer{};
        std::string nextBatch{};
        std::string messageType;
        std::string messageLengthBuffer;
        std::string messageContent;

        MessageBatch currentBatch{};

        std::condition_variable cvQueue;

        size_t messageContentLength{};
        size_t nbContentBytesRead{};

        unsigned int messageBatchSize;
        ParseState parseState{};
        rigtorp::MPMCQueue<std::string> queue;
        bool isNew{true};
    public:
        explicit TCPPublisherHandler(const int res, ServerContext* serverContext)
                : PubSubHandler(res, serverContext),
                  messageBatchSize(1),
                  queue(32)
        {}

        void printHello() override {
            std::cout << clientName << " | a publisher has connected" << std::endl << std::flush;
        }
    protected:
        void handle(struct io_uring *ring, int res) override {
            if (getIsDisconnected()) return;

            switch (event) {
                case Enums::Event_ReceivePublisherData:
                    onReceiveDataComplete(ring, res);
                    break;
                default:
                    break;
            }
        }

        void afterSendAckComplete(struct io_uring *ring) override {
            beginReceiveData(ring);
        }

        void onDisconnected (int res) override {
            std::cout << "Publisher disconnected [" << clientName << "]\n";
            setDisconnected();
        }
    public:
        [[nodiscard]] bool getIsNew() const override {
            return isNew;
        }

        void setIsNew(bool b) override {
            isNew = b;
        }

    private:
        static void pushToQueue(MessageBatch&& batch) {
            // std::cout << "Pushing message to queue: " << batch.getBufferRemaining() << std::endl;
            getMessageQueue().push_back(std::move(batch));
        }

        void beginReceiveData(struct io_uring* ring) {
            memset(readBuffer, 0, MAX_READ_BUF);
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event_ReceivePublisherData;
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
            } else {
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
                            pushToQueue(std::move(currentBatch));
                            currentBatch.clearForNextMessage();
                            currentBatch.setMessageType(messageType);
                        }

                        currentBatch.append(std::move(message));

                        if (i == (bufferLength-1)) {
                            pushToQueue(std::move(currentBatch));
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
    };
}

#endif //PUBLISHERHANDLER_HPP
