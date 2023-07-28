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
        std::string messageContentBuffer;
        size_t messageContentLength;

        size_t nbMessageBytesRead{};
        size_t nbContentBytesRead{};
    private:
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
            if (parseState == ParseState_messageContent) {
                if (readRestOfBuffer(0, res)) {
                    return;
                }
            } else {
                for (size_t i{0}; i < res; ++i) {
                    char ch {readBuffer[i]};
                    ++nbMessageBytesRead;
                    if (parseState == ParseState_messageType) {
                        if (ch == '|') {
                            parseState = ParseState_messageContentLength;
                            continue;
                        } else {
                            messageType.push_back(ch);
                        }
                    } else if (parseState == ParseState_messageContentLength) {
                        if (ch == '|') {
                            messageContentLength = std::stoul(messageContentBuffer);
                            parseState = ParseState_messageContent;
                            continue;
                        } else {
                            messageContentBuffer.push_back(ch);
                        }
                    } else if (parseState == ParseState_messageContent) {
                        if (!readRestOfBuffer(i, res - nbMessageBytesRead + 1)) {
                            beginReceiveData(ring);
                        }
                        return;
                    }
                }
            }

            beginReceiveData(ring);
        }

        /**
         * Reads the rest of readBuffer
         * @param startPos
         * @param nbChars
         * @return returns true if parsing is done
         */
        bool readRestOfBuffer(size_t startPos, size_t nbChars) {
            messageContent.append(&readBuffer[startPos], nbChars);

            nbContentBytesRead += nbChars;
            if ((nbContentBytesRead) == messageContentLength) {

                getPushService().push(std::move(messageType), std::move(messageContent));

                messageContentLength = 0;
                nbMessageBytesRead = 0;
                nbContentBytesRead = 0;
                messageContent.clear();
                messageContentBuffer.clear();
                messageType.clear();

                return true;
            }

            return false;
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
            }
        }
    };
}

#endif //GAZELLEMQ_MESSAGEPUBLISHER_HPP
