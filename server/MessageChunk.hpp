#ifndef GAZELLEMQ_SERVER_MESSAGECHUNK_HPP
#define GAZELLEMQ_SERVER_MESSAGECHUNK_HPP

#include <algorithm>
#include <cstring>
#include "Consts.hpp"

namespace gazellemq::server {
    struct MessageChunk {
        std::string messageType;
        char content[MAX_OUT_BUF]{};

        size_t i{};
        size_t n{};

        ~MessageChunk() {
            i = 0;
            n = 0;
        }

        MessageChunk() = default;

        MessageChunk(MessageChunk const& other)  noexcept {
            memmove(this->content, other.content, other.n);
            this->messageType.append(other.messageType);
            this->i = other.i;
            this->n = other.n;
        }

        MessageChunk(MessageChunk&& other)  noexcept {
            std::swap(this->content, other.content);
            std::swap(this->messageType, other.messageType);
            std::swap(this->i, other.i);
            std::swap(this->n, other.n);
        }

        MessageChunk& operator=(MessageChunk&& other) noexcept {
            std::swap(other.messageType, this->messageType);
            std::swap(other.content, this->content);
            std::swap(other.i, this->i);
            std::swap(other.n, this->n);

            return *this;
        }

        MessageChunk& operator=(MessageChunk const& other) noexcept {
            if (this == &other) {
                return *this;
            }

            memmove(this->content, other.content, other.n);
            this->messageType.append(other.messageType);
            this->i = other.i;
            this->n = other.n;

            return *this;
        }
    };
}

#endif //GAZELLEMQ_SERVER_MESSAGECHUNK_HPP
