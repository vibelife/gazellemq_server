#ifndef OANDA_CONNECTOR_SERVER_HTTPHEADERVALUE_HPP
#define OANDA_CONNECTOR_SERVER_HTTPHEADERVALUE_HPP

#include <cstring>

class HttpHeaderValue {
public:
    static constexpr unsigned int HEADER_VAL_MAX_LEN = 128;
    char value[HEADER_VAL_MAX_LEN]{};
    unsigned long nbChars{};

    /**
     * Returns a constructed HttpHeaderValue based on the passed in params
     * @param val
     * @param nbChars
     * @return
     */
    static HttpHeaderValue create(char const* val, unsigned long nbChars) {
        HttpHeaderValue retVal{};
        retVal.nbChars = nbChars;
        memmove(retVal.value, val, nbChars);
        return retVal;
    }
};

static constexpr HttpHeaderValue NONE = HttpHeaderValue{"", 0};

#endif //OANDA_CONNECTOR_SERVER_HTTPHEADERVALUE_HPP
