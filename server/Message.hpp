#ifndef GAZELLEMQ_SERVER_MESSAGE_HPP
#define GAZELLEMQ_SERVER_MESSAGE_HPP

#include <algorithm>
#include <cstring>

namespace gazellemq::server {
    struct Message {
        std::string messageType;
        std::string content;

        size_t i{};
        size_t n{};

        ~Message() {
            i = 0;
            n = 0;
        }

        Message() = default;

        Message(Message&& other)  noexcept {
            std::swap(this->content, other.content);
            std::swap(this->messageType, other.messageType);
            std::swap(this->i, other.i);
            std::swap(this->n, other.n);
        }

        Message& operator=(Message&& other) noexcept {
            std::swap(other.messageType, this->messageType);
            std::swap(other.content, this->content);
            std::swap(other.i, this->i);
            std::swap(other.n, this->n);

            return *this;
        }
    };
}

#endif //GAZELLEMQ_SERVER_MESSAGE_HPP
