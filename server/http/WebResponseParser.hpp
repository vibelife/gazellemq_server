#ifndef OANDA_CONNECTOR_SERVER_WEBRESPONSEPARSER_HPP
#define OANDA_CONNECTOR_SERVER_WEBRESPONSEPARSER_HPP

#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include "../Enums.hpp"
#include "HttpHeaderValue.hpp"
#include "../Consts.hpp"
#include "../StringUtils.hpp"

namespace gazellemq::server {
    class WebResponseParser {
    private:
        static constexpr auto KEEP_ALIVE_HEADER_SEARCH = "keep-alive:";
        static constexpr auto KEEP_ALIVE_HEADER_SEARCH_LEN = 11;
        static constexpr auto CONNECTION_HEADER_SEARCH = "connection:";
        static constexpr auto CONNECTION_HEADER_SEARCH_LEN = 11;
        static constexpr auto ETAG_HEADER_SEARCH = "etag:";
        static constexpr auto ETAG_HEADER_SEARCH_LEN = 5;
        static constexpr auto LAST_MODIFIED_HEADER_SEARCH = "last-modified:";
        static constexpr auto LAST_MODIFIED_HEADER_SEARCH_LEN = 14;
        static constexpr auto CONTENT_LENGTH_HEADER_SEARCH = "content-length:";
        static constexpr auto CONTENT_LENGTH_HEADER_SEARCH_LEN = 15;
        static constexpr auto CONTENT_TYPE_HEADER_SEARCH = "content-type:";
        static constexpr auto CONTENT_TYPE_HEADER_SEARCH_LEN = 13;
        static constexpr auto UPGRADE_HEADER_SEARCH = "upgrade:";
        static constexpr auto UPGRADE_HEADER_SEARCH_LEN = 8;
        static constexpr auto WS_KEY_HEADER_SEARCH = "sec-websocket-key:";
        static constexpr auto WS_KEY_HEADER_SEARCH_LEN = 18;


        enum HeaderLineParseStates {
            HeaderLineParseStates_SLASH_R,
            HeaderLineParseStates_SLASH_N,
            HeaderLineParseStates_DONE,
            HeaderLineParseStates_SKIP
        };

        enum EndResponseParseStates {
            EndResponseParseStates_notSet,
            EndResponseParseStates_r,
            EndResponseParseStates_rn,
            EndResponseParseStates_rnr,
            EndResponseParseStates_rnrn
        };

        enum HttpHeaderNames: int {
            CONTENT_LENGTH,
            CONTENT_TYPE,
            ETAG,
            LAST_MODIFIED,
            CONNECTION,
            UPGRADE,
            SEC_WEBSOCKET_KEY,
        };

        enum HttpResponseCodes: int {
            h204_no_content = 204,
            h304_not_modified = 304,
        };

        char currentLine[MAX_READ_BUF]{};
        size_t currentLineLen = 0;
        HeaderLineParseStates headerLineParseState{HeaderLineParseStates_SLASH_R};
        bool isFirstLine{true};
        bool isDoneParsing{false};
        bool canParseResponseBody{false};
        bool isInvalid{false};
        size_t contentLengthHeaderValue{};
        size_t nbCharsParsed{};
        char secWebSocketKey[WS_KEY_BUF]{};
        EndResponseParseStates currentEndState{EndResponseParseStates_notSet};
        EndResponseParseStates lastEndState{EndResponseParseStates_notSet};
    public:
        std::unordered_map<HttpHeaderNames, HttpHeaderValue> headers{};
        int responseCode{};
    public:
        /**
         * Returns the response code
         * @return
         */
        int getResponseCode() const {
            return responseCode;
        }

        /**
         * Returns true if the response body can be parsed
         * @return
         */
        bool canDownload() const {
            return canParseResponseBody;
        }

        /**
         * Returns a header value
         * @param name
         * @return
         */
        HttpHeaderValue&& getHeaderValue(HttpHeaderNames const& name) {
            return std::forward<HttpHeaderValue>(headers[name]);
        }

        /**
         * Returns a header value
         * @param name
         * @return
         */
        HttpHeaderValue copyHeaderValue(HttpHeaderNames const& name) {
            auto header = headers[name];
            return HttpHeaderValue::create(header.value, header.nbChars);
        }

        /**
         * Returns the content length header if it was set, or the number of characters parsed.
         * @return
         */
        long getContentLength() const {
            if (headers.contains(HttpHeaderNames::CONTENT_LENGTH)) {
                return static_cast<long>(contentLengthHeaderValue);
            } else {
                return isDoneParsing
                       ? nbCharsParsed
                       : -1;
            }
        }

        /**
         * Returns true if the push is complete
         * @return
         */
        bool getIsDoneParsing() const {
            return isDoneParsing;
        }

        /**
         * Returns the value of the websocket key if the proper headers were set, nullptr otherwise.
         * @return
         */
        char const* getSecWebSocketKey() const {
            if (headers.contains(HttpHeaderNames::SEC_WEBSOCKET_KEY) && headers.contains(HttpHeaderNames::UPGRADE))
                return secWebSocketKey;

            return nullptr;
        }

        /**
         * Resets this parser so it can be reused
         */
        void reset() {
            memset(currentLine, 0, MAX_READ_BUF);
            currentLineLen = 0;
            headerLineParseState = HeaderLineParseStates_SLASH_R;
            isFirstLine = true;
            isDoneParsing = false;
            canParseResponseBody = false;
            isInvalid = false;
            contentLengthHeaderValue = 0;
            nbCharsParsed = 0;
            currentEndState = EndResponseParseStates_notSet;
            lastEndState = EndResponseParseStates_notSet;
            headers.clear();
            responseCode = 0;
        }

        /**
         * Returns true if the header value was parsed. Header name/value pairs are put into [this->headers]
         * @param header
         * @param chars
         * @param nbChars
         * @return
         */
        bool parseHeader(HttpHeaderNames const& header, char const * const chars, size_t const& nbChars) {
            if ((!headers.contains(header)) && (currentLineLen >= nbChars) && (gazellemq::utils::hasPrefix(currentLine, currentLineLen, chars, nbChars))) {
                size_t nbValueChars = currentLineLen - nbChars;
                size_t offset{};
                // the following loop is just used to move past any leading white space in the header value
                while (((nbChars + offset) < currentLineLen) && isspace((int)currentLine[nbChars + offset])) {
                    ++offset;
                    --nbValueChars;
                }

                headers[header] = HttpHeaderValue::create(static_cast<char const*>(&currentLine[nbChars + offset]), nbValueChars);
                return true;
            }
            return false;
        }

        /**
         * Returns true if we were able to get the content-length header
         * @return
         */
        bool parseContentLengthHeader() {
            if (parseHeader(HttpHeaderNames::CONTENT_LENGTH, CONTENT_LENGTH_HEADER_SEARCH, CONTENT_LENGTH_HEADER_SEARCH_LEN)) {
                try {
                    contentLengthHeaderValue = std::stoul(headers[HttpHeaderNames::CONTENT_LENGTH].value);
                    if (contentLengthHeaderValue < 1) {
                        canParseResponseBody = false;
                    }
                    return true;
                } catch (std::exception const& ex) {
                    // TODO - content-length header could not be determined. Should we read until some limit?
                    contentLengthHeaderValue = 0;
                    canParseResponseBody = false;
                }
            }
            return false;
        }

        /**
         * Tries to parse the connection header
         * @return
         */
        bool parseConnectionHeader() {
            if (parseHeader(HttpHeaderNames::CONNECTION, CONNECTION_HEADER_SEARCH, CONNECTION_HEADER_SEARCH_LEN)) {
                std::string_view connection{headers[HttpHeaderNames::CONNECTION].value};
                if (!gazellemq::utils::compare(connection, "keep-alive")) {
                    // TODO - server is not accepting keep-alive connections!
                }
                return true;
            }
            return false;
        }

        /**
         * Tries to parse the etag header
         * @return
         */
        bool parseETagHeader() {
            if (parseHeader(HttpHeaderNames::ETAG, ETAG_HEADER_SEARCH, ETAG_HEADER_SEARCH_LEN)) {
                return true;
            }
            return false;
        }

        /**
         * Tries to parse the etag header
         * @return
         */
        bool parseLastModifiedHeader() {
            if (parseHeader(HttpHeaderNames::LAST_MODIFIED, LAST_MODIFIED_HEADER_SEARCH, LAST_MODIFIED_HEADER_SEARCH_LEN)) {
                return true;
            }
            return false;
        }

        /**
         * Tries to parse the content-type header
         * @return
         */
        bool parseContentTypeHeader() {
            if (parseHeader(HttpHeaderNames::CONTENT_TYPE, CONTENT_TYPE_HEADER_SEARCH, CONTENT_TYPE_HEADER_SEARCH_LEN)) {
                return true;
            }
            return false;
        }

        /**
         * Tries to parse the upgrade header
         * @return
         */
        bool parseUpgradeHeader() {
            if (parseHeader(HttpHeaderNames::UPGRADE, UPGRADE_HEADER_SEARCH, UPGRADE_HEADER_SEARCH_LEN)) {
                return true;
            }
            return false;
        }

        /**
         * Tries to parse the upgrade header
         * @return
         */
        bool parseSecWebSocketKeyHeader() {
            if (parseHeader(HttpHeaderNames::SEC_WEBSOCKET_KEY, WS_KEY_HEADER_SEARCH, WS_KEY_HEADER_SEARCH_LEN)) {
                std::string_view key{headers[HttpHeaderNames::SEC_WEBSOCKET_KEY].value};
                memset(secWebSocketKey, 0, WS_KEY_BUF);
                key.copy(secWebSocketKey, key.length());
                return true;
            }
            return false;
        }

        /**
         * Parses the first line of the response
         */
        void parseFirstLine() {
            // http/1.1 200 OK
            // in the above response code line we just skip to the 9th character and try to read the number
            try {
                responseCode = static_cast<int>(strtol(&currentLine[9], nullptr, 10));
            } catch (...) {
                // TODO figure out what to do here
                responseCode = 0;
                isInvalid = true;
            }

            if (responseCode == HttpResponseCodes::h304_not_modified) {
                // 304 NOT MODIFIED
                headerLineParseState = HeaderLineParseStates_SKIP;
            } else if (responseCode == HttpResponseCodes::h204_no_content) {
                // 204 NO CONTENT
                headerLineParseState = HeaderLineParseStates_SKIP;
            } else if (responseCode > 199 && responseCode < 300) {
                // must download
                canParseResponseBody = true;
            }  else {
                // some error
            }
        }

        /**
         * Processes a full header line
         */
        [[nodiscard]] bool processLine() {
            bool isDone{false};

            if (isFirstLine) {
                isFirstLine = false;
                parseFirstLine();
            } else {
                if (currentLineLen == 0) {
                    // done
                    isDone = true;
                } else {
                    parseUpgradeHeader()
                    || parseSecWebSocketKeyHeader()
                    || parseContentLengthHeader()
                    || parseConnectionHeader();
                }
            }

            memset(currentLine, 0, MAX_READ_BUF);
            currentLineLen = 0;
            return isDone;
        }

        /**
         * Consumes chars and advances the [start] pointer
         * @param start
         * @param end
         */
        void consume(char *&start, char const * const end) {
            while (start != end) {
                currentLine[currentLineLen++] = *start++;
            }
        }

        /**
         * Parses out to the /r
         * @param resp
         * @param nbChars
         * @param ch
         * @param chSlashR
         */
        void gotoSlashR(char const * const resp, size_t nbChars, char *&ch, char *chSlashR) {
            size_t n = nbChars - (ch - resp);
            if ((chSlashR = (char*)memchr(ch, '\r', n)) != nullptr) {
                // consume up to the \r
                consume(ch, chSlashR);
                // move past the \r
                ++ch;

                // next we need to look for the \n
                headerLineParseState = HeaderLineParseStates_SLASH_N;
            } else {
                // line does not end with "\r",  so we consume everything
                // next time through will most likely pick up the \r
                consume(ch, ch + n);
            }
        }

        /**
         * Parses out to the /n
         * @param ch
         */
        void gotoSlashN(char *&ch) {
            if (*ch == '\n') {
                if (processLine()) {
                    headerLineParseState = HeaderLineParseStates_DONE;

                    // check if this is a websocket upgrade request
                    // check if we even need to parse the response body
                    if (headers.contains(HttpHeaderNames::SEC_WEBSOCKET_KEY) && headers.contains(HttpHeaderNames::UPGRADE)) {
                        isDoneParsing = true;
                    } else if (!canParseResponseBody && contentLengthHeaderValue == 0 && headers.contains(HttpHeaderNames::CONTENT_LENGTH)) {
                        isDoneParsing = true;
                    }
                } else {
                    if (headerLineParseState != HeaderLineParseStates_SKIP) {
                        headerLineParseState = HeaderLineParseStates_SLASH_R;
                    }
                }
                ++ch;
            } else {
                // TODO malformed!
                isInvalid = true;
            }
        }

        void writeToOutBuffer(char * const start, char * const end, char *&content, size_t &chunkSize) {
            size_t nbChars{size_t(end - start)};
            chunkSize = static_cast<size_t>(lastEndState) + nbChars;
            content = static_cast<char*>(calloc(chunkSize + 1, sizeof(char)));

            switch (lastEndState) {
                case EndResponseParseStates_rnr:
                    memmove(&content[0], "\r\n\r", 3);
                    memmove(&content[3], start, nbChars);
                    break;
                case EndResponseParseStates_rn:
                    memmove(&content[0], "\r\n", 2);
                    memmove(&content[2], start, nbChars);
                    break;
                case EndResponseParseStates_r:
                    memmove(&content[0], "\r", 1);
                    memmove(&content[1], start, nbChars);
                    break;
                default:
                    memmove(&content[0], start, nbChars);
                    break;
            }

            lastEndState = EndResponseParseStates_notSet;
            nbCharsParsed += chunkSize;

            // We are done if any of the following are true
            // 1) Have we hit the end of the content (\r\n\r\n)?
            // 2) Parsed enough bytes (content-length header)?
            if ((contentLengthHeaderValue > 0 && nbCharsParsed == contentLengthHeaderValue) || currentEndState == EndResponseParseStates_rnrn) {
                isDoneParsing = true;
            }
        }

        /**
         * Reads to the end of the response body
         * @param ch
         * @param end
         * @param content
         * @param chunkSize
         */
        void gotoEndOfResponse(char*& ch, char * const end, char *&content, size_t &chunkSize) {
            if (!canParseResponseBody) {
                isDoneParsing = true;
                ch = end;
                return;
            }

            size_t n{size_t(end - ch)};

            if (currentEndState == EndResponseParseStates_rnrn) {
                isDoneParsing = true;
                ch = end;
            } else if (currentEndState == EndResponseParseStates_notSet) {
                if (memcmp(end - 4, "\r\n\r\n", 4) == 0) {
                    currentEndState = EndResponseParseStates_rnrn;
                } else if (memcmp(end - 3, "\r\n\r", 3) == 0) {
                    currentEndState = EndResponseParseStates_rnr;
                } else if (memcmp(end - 2, "\r\n", 2) == 0) {
                    currentEndState = EndResponseParseStates_rn;
                } else if (memcmp(end - 1, "\r", 1) == 0) {
                    currentEndState = EndResponseParseStates_r;
                }

                writeToOutBuffer(ch, end - static_cast<int>(currentEndState), content, chunkSize);
                ch = end;
            } else if (currentEndState == EndResponseParseStates_r) {
                if (n >= 3 && ch != nullptr && memcmp(ch, "\n\r\n", 3) == 0) {
                    currentEndState = EndResponseParseStates_rnrn;
                    writeToOutBuffer(ch, end - 3, content, chunkSize);
                } else if (n >= 2 && ch != nullptr && memcmp(ch, "\n\r", 2) == 0) {
                    currentEndState = EndResponseParseStates_rnr;
                } else if (n >= 1 && ch != nullptr && memcmp(ch, "\n", 1) == 0) {
                    currentEndState = EndResponseParseStates_rn;
                } else {
                    // not the end sequence
                    lastEndState = currentEndState;
                    currentEndState = EndResponseParseStates_notSet;
                }
            } else if (currentEndState == EndResponseParseStates_rn) {
                if (n >= 2 && ch != nullptr && memcmp(ch, "\r\n", 2) == 0) {
                    currentEndState = EndResponseParseStates_rnrn;
                    writeToOutBuffer(ch, end - 2, content, chunkSize);
                } else if (n >= 1 && ch != nullptr && memcmp(ch, "\r", 1) == 0) {
                    currentEndState = EndResponseParseStates_rnr;
                } else {
                    // not the end sequence
                    lastEndState = currentEndState;
                    currentEndState = EndResponseParseStates_notSet;
                }
            } else if (currentEndState == EndResponseParseStates_rnr) {
                if (n >= 1 && ch != nullptr && memcmp(ch, "\n", 1) == 0) {
                    currentEndState = EndResponseParseStates_rnrn;
                    writeToOutBuffer(ch, end - 1, content, chunkSize);
                } else {
                    // not the end sequence
                    lastEndState = currentEndState;
                    currentEndState = EndResponseParseStates_notSet;
                }
            }
        }

        /**
         * Use this is cases where there is no more bytes to read and you need to treat the response as fully parsed.
         */
        void forceComplete() {
            isDoneParsing = true;
        }

        /**
         * Parses the HTTP response and extracts the necessary parts from it
         * @param responseFragment
         * @param nbChars
         * @param content
         * @param chunkSize
         * @return
         */
        VResult parse(char * const resp, size_t const& nbChars, char*& content, size_t &chunkSize) {
            if (nbChars == 0 || isDoneParsing) return V_SUCCEEDED;

            char *ch = resp;
            char * const respEnd = (resp + nbChars);
            char *chSlashR = nullptr;

            do {
                switch (headerLineParseState) {
                    case HeaderLineParseStates_SLASH_R:
                        gotoSlashR(resp, nbChars, ch, chSlashR);
                        break;
                    case HeaderLineParseStates_SLASH_N:
                        gotoSlashN(ch);
                        break;
                    case HeaderLineParseStates_DONE:
                        // to get here means we have parsed all headers, and now we are at the response body
                        gotoEndOfResponse(ch, respEnd, content, chunkSize);
                        break;
                    case HeaderLineParseStates_SKIP:
                        // skip right to the end of the content
                        ch = chSlashR;
                        gotoEndOfResponse(ch, respEnd, content, chunkSize);
                        break;
                }
            } while ((ch != respEnd) && !isInvalid);

            if (isInvalid) {
                return V_FAILED;
            }

            if (getIsDoneParsing()) {
                return V_SUCCEEDED;
            }

            return V_RETRY;
        }

    };
}

#endif //OANDA_CONNECTOR_SERVER_WEBRESPONSEPARSER_HPP
