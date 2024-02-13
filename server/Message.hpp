#ifndef GAZELLEMQ_SERVER_MESSAGE_HPP
#define GAZELLEMQ_SERVER_MESSAGE_HPP

#include <algorithm>
#include <cstring>
#include "Consts.hpp"

namespace gazellemq::server {
    struct Message {
    private:
        size_t i{};
        size_t n{};
        bool isBusy{};
        std::string messageType;
        std::string content;
    public:
        Message() noexcept = default;;

        Message(std::string messageType, std::string const& buffer) noexcept
            : messageType(std::move(messageType)), n(buffer.size()), content(buffer), i(0)
            {}

        Message(Message const& other)  noexcept {
            this->content.append(other.content);
            this->messageType.append(other.messageType);
            this->i = other.i;
            this->n = other.n;
            this->isBusy = other.isBusy;
        }

        Message(Message&& other)  noexcept {
            std::swap(this->content, other.content);
            std::swap(this->messageType, other.messageType);
            std::swap(this->i, other.i);
            std::swap(this->n, other.n);
            std::swap(this->isBusy, other.isBusy);
        }

        Message& operator=(Message&& other) noexcept {
            std::swap(other.messageType, this->messageType);
            std::swap(other.content, this->content);
            std::swap(other.i, this->i);
            std::swap(other.n, this->n);
            std::swap(other.isBusy, this->isBusy);

            return *this;
        }

        Message& operator=(Message const& other) noexcept {
            if (this == &other) {
                return *this;
            }

            this->content.append(other.content);
            this->messageType.append(other.messageType);
            this->i = other.i;
            this->n = other.n;
            this->isBusy = other.isBusy;

            return *this;
        }

        [[nodiscard]] std::string getMessageType() const {
            return messageType;
        }

        [[nodiscard]] char const* getContentRemaining() const {
            return &content.c_str()[i];
        }

        [[nodiscard]] size_t getContentRemainingLength() const {
            return n;
        }

        void advance(size_t nbChars) {
            i += nbChars;
            n -= nbChars;
        }

        [[nodiscard]] bool isDone() const {
            return n == 0;
        }

        [[nodiscard]] bool hasContent() const {
            return !content.empty();
        }

        void setBusy() {
            isBusy = true;
        }

        void clear() {
            this->content.clear();
            this->messageType.clear();
            this->n = 0;
            this->i = 0;
            this->isBusy = false;
        }
    };
}

#endif //GAZELLEMQ_SERVER_MESSAGE_HPP
