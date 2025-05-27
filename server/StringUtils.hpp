#ifndef GAZELLEMQ_SERVER_STRINGUTILS_HPP
#define GAZELLEMQ_SERVER_STRINGUTILS_HPP

#include <string>
#include <vector>
#include <cstring>

namespace gazellemq::utils {
    static constexpr auto TO_LOWER_CHAR_MATCH = [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    };

    /**
     * Case insensitive check
     * @param a
     * @param b
     * @return
     */
    static bool compare(std::string_view a, std::string_view b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), b.end(), TO_LOWER_CHAR_MATCH);
    }

    /**
     * Returns true if the passed in [haystack] starts with [needle]. You must convert to the same case yourself.
     * @param haystack
     * @param haystackLen
     * @param needle
     * @param needleLen
     * @return
     */
    static bool hasPrefix(char const *haystack, unsigned long haystackLen, char const *needle, unsigned long needleLen) {
        // return (haystackLen >= needleLen) && memcmp(haystack, needle, needleLen) == 0;
        return haystackLen >= needleLen && compare({haystack, needleLen}, {needle, needleLen});
    }

    /**
     * Splits a string
     * @param input
     * @param strings
     * @param delimiter
     * @return
     */
    inline void split(std::string &&input, std::vector<std::string>& strings, char const delimiter = ',') {
        std::string buf;
        buf.reserve(input.size());

        for (size_t i{}; i < input.size(); ++i) {
            if (input[i] != delimiter) {
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
