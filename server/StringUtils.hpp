#ifndef GAZELLEMQ_SERVER_STRINGUTILS_HPP
#define GAZELLEMQ_SERVER_STRINGUTILS_HPP

#include <string>
#include <vector>
#include <cstring>

namespace gazellemq::utils {
    /**
     * Splits a string
     * @param input
     * @return
     */
    void split(std::string &&input, std::vector<std::string>& strings, std::string &&prefix = "", char delimiter = ',') {
        std::string buf;
        buf.reserve(input.size());

        for (size_t i{}; i < input.size(); ++i) {
            if (input[i] != ',') {
                buf.push_back(input[i]);
            } else {
                strings.push_back(std::move(std::string{buf}));
                buf.clear();
            }
        }

        if (!buf.empty()) {
            strings.push_back(std::move(buf));
        }
    }
}

#endif //GAZELLEMQ_SERVER_STRINGUTILS_HPP
