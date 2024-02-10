#ifndef GAZELLEMQ_MESSAGEPUBLISHER_HPP
#define GAZELLEMQ_MESSAGEPUBLISHER_HPP

#include "MessageHandler.hpp"
#include "PushService.hpp"

#include "Consts.hpp"

namespace gazellemq::server {
    class MessagePublisher : public MessageHandler {
    private:
        enum MessagePublisherState {
            MessagePublisherState_notSet,
            MessagePublisherState_receiveData,
            MessagePublisherState_disconnect,
            MessagePublisherState_sendAck,
        };

        char readBuffer[MAX_READ_BUF]{};
        std::string content;

        MessagePublisherState state{MessagePublisherState_notSet};

        enum ParseState {
            ParseState_messageType,
            ParseState_messageContentLength,
            ParseState_messageContent,
        };

        ParseState parseState{ParseState_messageType};

        std::string messageType;
        std::string messageLengthBuffer;
        size_t messageContentLength;
        size_t nbContentBytesRead{};
        char outBuffer[MAX_OUT_BUF]{};
        size_t outBufferLen{};
    private:
        /**
         * Disconnects from the server
         * @param ring
         */
        void beginDisconnect(struct io_uring* ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_close(sqe, fd);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            state = MessagePublisherState_disconnect;
            io_uring_submit(ring);
        }

        void onDisconnected(struct io_uring *ring, int res) {
            printf("A publisher disconnected\n");
            markForRemoval();
        }


        /**
         * Sends an acknowledgement to the client
         * @param ring
         */
        void beginSendAck(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, "\r", 1, 0);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

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
            if (res == 0) {
                // The client has disconnected
                beginDisconnect(ring);
            } else {
                streamMessage(readBuffer, res);

                beginReceiveData(ring);
            }
        }

        /**
         * Streams the message in chunks to subscribers
         * @param buffer
         * @param bufferLength
         */
        void streamMessage(char const* buffer, size_t bufferLength) {
            for (size_t i{0}; i < bufferLength; ++i) {
                char ch {buffer[i]};
                if (parseState == ParseState_messageType) {
                    if (ch == '|') {
                        memmove(&outBuffer[outBufferLen++], "|", 1);
                        parseState = ParseState_messageContentLength;
                        continue;
                    } else {
                        messageType.push_back(ch);
                        memmove(&outBuffer[outBufferLen++], &ch, 1);
                    }
                } else if (parseState == ParseState_messageContentLength) {
                    if (ch == '|') {
                        messageContentLength = std::stoul(messageLengthBuffer);
                        memmove(&outBuffer[outBufferLen++], "|", 1);
                        parseState = ParseState_messageContent;
                        continue;
                    } else {
                        messageLengthBuffer.push_back(ch);
                        memmove(&outBuffer[outBufferLen++], &ch, 1);
                    }
                } else if (parseState == ParseState_messageContent) {
                    size_t nbCharsNeeded {messageContentLength - nbContentBytesRead};
                    // add as many characters as possible in bulk

                    if ((i + nbCharsNeeded) <= bufferLength) {
                        // do nothing here for now
                    } else {
                        nbCharsNeeded = bufferLength - i;
                    }

                    nbContentBytesRead += nbCharsNeeded;
                    addToOutBuffer(&buffer[i], nbCharsNeeded);
                    i += nbCharsNeeded - 1;

                    if (messageContentLength == nbContentBytesRead) {
                        // Done parsing

                        messageContentLength = 0;
                        nbContentBytesRead = 0;
                        messageLengthBuffer.clear();
                        messageType.clear();
                        parseState = ParseState_messageType;
                    }
                }
            }
        }

        void addToOutBuffer(char const* buffer, size_t len) {
            size_t nbChars{len};
            size_t i{0};
            size_t nbCharsToAdd{0};

            while (nbChars > 0) {
                if ((outBufferLen + nbChars) > MAX_OUT_BUF) {
                    nbCharsToAdd = MAX_OUT_BUF - outBufferLen;
                } else {
                    nbCharsToAdd = nbChars;
                }

                memmove(&outBuffer[outBufferLen], &buffer[i], nbCharsToAdd);
                outBufferLen += nbCharsToAdd;
                i += nbCharsToAdd;
                nbChars -= nbCharsToAdd;

                if (outBufferLen == MAX_OUT_BUF || (nbChars <= 0 && messageContentLength == nbContentBytesRead)) {
                    getPushService().pushToQueue(messageType, outBuffer, outBufferLen);

                    memset(outBuffer, 0, MAX_OUT_BUF);
                    outBufferLen = 0;
                }
            }
        }

    public:
        explicit MessagePublisher(int fileDescriptor)
            :MessageHandler(fileDescriptor)
        {}

        ~MessagePublisher() override = default;

        void printHello() const override {
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
            }
        }
    };
}

#endif //GAZELLEMQ_MESSAGEPUBLISHER_HPP
