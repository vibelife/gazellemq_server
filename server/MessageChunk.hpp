#ifndef GAZELLEMQ_SERVER_MESSAGECHUNK_HPP
#define GAZELLEMQ_SERVER_MESSAGECHUNK_HPP

#include <algorithm>
#include <cstring>
#include "Consts.hpp"

namespace gazellemq::server {
    struct MessageChunk {
    private:
        static constexpr size_t maxContentLength = MAX_READ_BUF;
        bool canRemove{false};
    public:
        std::string messageType;
        std::string content;

        size_t i{};
        size_t n{};
        bool isBusy{};

        ~MessageChunk() {
            i = 0;
            n = 0;
        }

        MessageChunk() noexcept = default;;

        MessageChunk(std::string messageType, std::string const& buffer) noexcept
            : messageType(std::move(messageType)), n(buffer.size()), content(buffer), i(0)
            {}

        MessageChunk(MessageChunk const& other)  noexcept {
            this->content.append(other.content);
            this->messageType.append(other.messageType);
            this->i = other.i;
            this->n = other.n;
            this->isBusy = other.isBusy;
            this->canRemove = other.canRemove;
        }

        MessageChunk(MessageChunk&& other)  noexcept {
            std::swap(this->content, other.content);
            std::swap(this->messageType, other.messageType);
            std::swap(this->i, other.i);
            std::swap(this->n, other.n);
            std::swap(this->isBusy, other.isBusy);
            std::swap(this->canRemove, other.canRemove);
        }

        MessageChunk& operator=(MessageChunk&& other) noexcept {
            std::swap(other.messageType, this->messageType);
            std::swap(other.content, this->content);
            std::swap(other.i, this->i);
            std::swap(other.n, this->n);
            std::swap(other.isBusy, this->isBusy);
            std::swap(other.canRemove, this->canRemove);

            return *this;
        }

        MessageChunk& operator=(MessageChunk const& other) noexcept {
            if (this == &other) {
                return *this;
            }

            this->content.append(other.content);
            this->messageType.append(other.messageType);
            this->i = other.i;
            this->n = other.n;
            this->isBusy = other.isBusy;
            this->canRemove = other.canRemove;

            return *this;
        }

        bool tryTake(MessageChunk& other) {
            if ((this->messageType.empty() || this->messageType == other.messageType)
                && ((n + other.n < maxContentLength) || content.empty())
                && !isBusy
            ) {
                if (this->messageType.empty()) {
                    this->messageType.append(other.messageType);
                }
                this->content.append(other.content);
                this->n += other.n;
                other.canRemove = true;

                return true;
            }

            return false;
        }

        bool tryAppend(MessageChunk const& other) {
            if ((this->messageType.empty() || this->messageType == other.messageType)
                && ((n + other.n < maxContentLength) || content.empty())
                && !isBusy
            ) {
                if (this->messageType.empty()) {
                    this->messageType.append(other.messageType);
                }
                this->content.append(other.content);
                this->n += other.n;

                return true;
            }

            return false;
        }

        [[nodiscard]] bool getCanRemove() const {
            return canRemove;
        }

        void clear() {
            this->content.clear();
            this->messageType.clear();
            this->n = 0;
            this->i = 0;
            this->isBusy = false;
            this->canRemove = false;
        }
    };
}

#endif //GAZELLEMQ_SERVER_MESSAGECHUNK_HPP
