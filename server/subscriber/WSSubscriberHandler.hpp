#ifndef GAZELLEMQ_SERVER_WSSUBSCRIBERHANDLER_HPP
#define GAZELLEMQ_SERVER_WSSUBSCRIBERHANDLER_HPP

#include <openssl/sha.h>

#include "TCPSubscriberHandler.hpp"
#include "../http/WebResponseParser.hpp"

namespace gazellemq::server {
    class WSSubscriberHandler final : public TCPSubscriberHandler {
    protected:
        WebResponseParser webResponseParser{};
        char wsHandshakeWriteBuffer[MAX_HANDSHAKE_BUF];
        size_t handshakeOffset{};
        size_t handshakeLength{};
    public:
        WSSubscriberHandler(int res, ServerContext* serverContext)
                : TCPSubscriberHandler(res, serverContext)
        {}
    protected:
        void onDisconnected (int res) override {
            std::cout << "WebSocket disconnected [" << clientName << "]\n";
            setDisconnected();
        }


        /**
         * This client must now indicate whether it is a publisher or subscriber
         * @param ring
         * @param res
         */
        void onMakeNonblockingSocketComplete(struct io_uring* ring, int res) override {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__ , res);
                beginDisconnect(ring);
            } else {

                // client must communicate with server
                this->beginReceiveHttpUpgrade(ring);
            }
        }

        /**
         * WebSocket clients are expected to send and updage request
         * @param ring
         */
        void beginReceiveHttpUpgrade(struct io_uring* ring) {
            memset(readBuffer, 0, MAX_READ_BUF);
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, 2, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event::Event_ReceiveHttpUpgrade;
            io_uring_submit(ring);
        }

        void onReceiveHttpUpgradeComplete(struct io_uring* ring, int const res) {
            if (res == 0) {
                // no data
            } else if (res < 0) {
                printError(__PRETTY_FUNCTION__ , res);
                beginDisconnect(ring);
            } else {
                char* dest{};
                size_t destLen;
                auto result{webResponseParser.parse(readBuffer, res, dest, destLen)};
                memset(readBuffer, 0, MAX_READ_BUF);

                switch (result) {
                    case V_SUCCEEDED:
                        prepSendWSResponse(ring);
                        break;
                    case V_RETRY:
                        beginReceiveHttpUpgrade(ring);
                        break;
                    case V_FAILED:
                        break;
                }

                if (dest != nullptr) {
                    free(dest);
                }
            }
        }

        static void writeToBuffer(char* dest, char const* const src, size_t& n) {
            size_t const srcLen{strlen(src)};
            memcpy(&dest[n], src, srcLen);
            n += srcLen;
        }

        void prepSendWSResponse(struct io_uring* ring) {
            memset(wsHandshakeWriteBuffer, 0, MAX_HANDSHAKE_BUF);
            writeToBuffer(wsHandshakeWriteBuffer, "HTTP/1.1 101 Switching Protocols\r\n", handshakeOffset);
            writeToBuffer(wsHandshakeWriteBuffer, "upgrade: websocket\r\n", handshakeOffset);
            writeToBuffer(wsHandshakeWriteBuffer, "connection: upgrade\r\n", handshakeOffset);
            writeToBuffer(wsHandshakeWriteBuffer, "Sec-WebSocket-Accept: ", handshakeOffset);

            // TODO: append 258EAFA5-E914-47DA-95CA-C5AB0DC85B11
            std::string key{webResponseParser.getSecWebSocketKey()};
            key.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
            unsigned char hash[MAX_HANDSHAKE_BUF]{};
            SHA1(reinterpret_cast<const unsigned char *>(key.c_str()), key.length(), hash);

            writeToBuffer(wsHandshakeWriteBuffer, "TODO", handshakeOffset);
            writeToBuffer(wsHandshakeWriteBuffer, "\r\n", handshakeOffset);

            handshakeLength = handshakeOffset;
            handshakeOffset = 0;
        }

        void beginSendWSResponse(struct io_uring* ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, &wsHandshakeWriteBuffer[handshakeOffset], handshakeLength - handshakeOffset, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event::Event_SendWSHandshake;
            io_uring_submit(ring);
        }

        void onSendWSResponseComplete(struct io_uring* ring, int const res) {
            if (res == 0) {
                // no data
            } else if (res < 0) {
                printError(__PRETTY_FUNCTION__, res);
                beginDisconnect(ring);
            } else {
                handshakeOffset += res;
                if (handshakeOffset < handshakeLength) {
                    beginSendWSResponse(ring);
                } else {
                    // done sending handshake
                }
            }
        }

        void handleEvent(struct io_uring *ring, int const res) override {
            if (this->getIsDisconnected()) return;

            switch (event) {
                case Enums::Event::Event_NotSet:
                    beginMakeNonblockingSocket(ring, res);
                    break;
                case Enums::Event::Event_SetNonblockingPublisher:
                    onMakeNonblockingSocketComplete(ring, res);
                    break;
                case Enums::Event::Event_ReceiveHttpUpgrade:
                    onReceiveHttpUpgradeComplete(ring, res);
                    break;
                case Enums::Event::Event_Disconnected:
                    onDisconnected(res);
                    break;
                default:
                    handle(ring, res);
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_SERVER_WSSUBSCRIBERHANDLER_HPP
