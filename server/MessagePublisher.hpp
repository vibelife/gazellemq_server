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
        std::string messageContent;
        std::string messageLengthBuffer;
        size_t messageContentLength;

        size_t nbMessageBytesRead{};
        size_t nbContentBytesRead{};
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
        }

        /**
         * Receives data from the publisher
         * @param ring
         */
        void beginReceiveData(struct io_uring *ring) {
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
                parseMessage(ring, readBuffer, res);

                beginReceiveData(ring);
            }
        }

        void parseMessage(struct io_uring *ring, char const* buffer, size_t bufferLength) {
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
                    messageContent.push_back(ch);
                    ++nbContentBytesRead;

                    if (messageContentLength == nbContentBytesRead) {
                        // Done parsing
                        getPushService().push(ring, std::move(messageType), std::move(messageContent));

                        messageContentLength = 0;
                        nbContentBytesRead = 0;
                        messageContent.clear();
                        messageLengthBuffer.clear();
                        messageType.clear();
                        parseState = ParseState_messageType;
                    }
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
                    beginReceiveData(ring);
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
