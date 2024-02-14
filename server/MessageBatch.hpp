#ifndef GAZELLEMQ_SERVER_MESSAGEBATCH_HPP
#define GAZELLEMQ_SERVER_MESSAGEBATCH_HPP

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>

namespace gazellemq::server {
    struct MessageBatch {
    private:
        static constexpr size_t SIZEOF_CHAR = sizeof(char);
        static constexpr size_t GROW_UNTIL_LENGTH = DEFAULT_BUF_LENGTH * 128;
        std::string messageType{};
        char *buffer{nullptr};
        size_t maxLength{};
        size_t bufferLength{};
        size_t bufferPosition{};
        unsigned int nbMessages{};
        bool isBusy{};
    public:
        MessageBatch(MessageBatch const &) = delete;

        MessageBatch &operator=(MessageBatch const &) = delete;

        MessageBatch(): maxLength(DEFAULT_BUF_LENGTH) {
            buffer = (char *) calloc(maxLength, sizeof(char));
        }

        MessageBatch(MessageBatch &&other) noexcept {
            this->swap(std::move(other));
        }

        MessageBatch &operator=(MessageBatch &&other) noexcept {
            this->swap(std::move(other));
            return *this;
        }

        ~MessageBatch() {
            if (buffer != nullptr) {
                free(buffer);
            }
        }
    private:
        void swap(MessageBatch &&other) noexcept {
            std::swap(this->buffer, other.buffer);
            std::swap(this->maxLength, other.maxLength);
            std::swap(this->bufferLength, other.bufferLength);
            std::swap(this->bufferPosition, other.bufferPosition);
            std::swap(this->messageType, other.messageType);
            std::swap(this->nbMessages, other.nbMessages);
            std::swap(this->isBusy, other.isBusy);
        }
    public:
        /**
         * Tries to append the passed in chars. Returns false if the chars cannot fit in the remaining buffer space.
         * @param chars
         * @param len
         * @return
         */
        bool append(std::string&& message) {
            if (bufferLength >= GROW_UNTIL_LENGTH) {
                return false;
            }

            if (message.size() + bufferLength > maxLength) {
                size_t byteSize{static_cast<size_t>((std::ceil(static_cast<double>(maxLength + message.size()) / DEFAULT_BUF_LENGTH) * DEFAULT_BUF_LENGTH))};
                buffer = (buffer == nullptr)
                    ? (char *) calloc(byteSize, SIZEOF_CHAR)
                    : (char *) realloc(buffer, byteSize);
                maxLength += message.size();
            }

            memmove(&buffer[bufferLength], message.c_str(), SIZEOF_CHAR * message.size());
            bufferLength += message.size();

            return true;
        }

        void copy(MessageBatch const& other) {
            if (other.bufferLength + bufferLength > maxLength) {
                size_t byteSize{static_cast<size_t>((std::ceil(static_cast<double>(maxLength + other.maxLength) / DEFAULT_BUF_LENGTH) * DEFAULT_BUF_LENGTH))};
                buffer = (buffer == nullptr)
                         ? (char *) calloc(byteSize, SIZEOF_CHAR)
                         : (char *) realloc(buffer, byteSize);
                maxLength += other.bufferLength;
            }

            memmove(&buffer[bufferLength], other.buffer, SIZEOF_CHAR * other.bufferLength);
            bufferLength += other.bufferLength;

            this->messageType = other.messageType;
            this->nbMessages = other.nbMessages;
            this->bufferPosition = 0;
            this->isBusy = false;
        }

        [[nodiscard]] MessageBatch copy() const {
            MessageBatch retVal{};
            retVal.copy(*this);
            return std::move(retVal);
        }

        /**
         * Returns the number of characters in the buffer
         * @return
         */
        [[nodiscard]] size_t getBufferLength() const {
            return bufferLength;
        }

        /**
         * Returns the messageType
         * @return
         */
        [[nodiscard]] std::string getMessageType() const {
            return messageType;
        }

        /**
         * Returns the buffer starting from the read position
         * @return
         */
        [[nodiscard]] char const* getBufferRemaining() const {
            return &buffer[bufferPosition];
        }

        /**
         * Advances the read position
         * @param amount
         */
        void advance(size_t amount) {
            bufferPosition = (bufferPosition + amount) <= maxLength ? (bufferPosition + amount) : maxLength;
            bufferLength = (bufferLength - amount) >= 0 ? (bufferLength - amount) : 0;
        }

        void clearForNextMessage() {
            if (this->buffer != nullptr) {
                memset(this->buffer, 0, this->maxLength);
            }

            this->bufferLength = 0;
            this->bufferPosition = 0;
            this->nbMessages = 0;
            this->isBusy = false;
        }

        [[nodiscard]] bool getIsBusy() const {
            return isBusy;
        }

        [[nodiscard]] bool isFull() const {
            return bufferLength > GROW_UNTIL_LENGTH;
        }

        void setBusy() {
            isBusy = true;
        }

        void setMessageType(std::string const& value) {
            this->messageType = value;
        }

        [[nodiscard]] bool getIsDone() const {
            return bufferLength == 0;
        }

        [[nodiscard]] bool hasContent() const {
            return bufferLength > 0;
        }
    };
}

#endif //GAZELLEMQ_SERVER_MESSAGEBATCH_HPP
