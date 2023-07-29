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
        char *s = input.data();
        char *word = strtok(s, &delimiter);

        while (word) {
            std::string val{prefix};
            val.append(word);
            strings.emplace_back(std::move(val));
            word = strtok(nullptr, &delimiter);
        }
        input.clear();
    }
}

#endif //GAZELLEMQ_SERVER_STRINGUTILS_HPP
