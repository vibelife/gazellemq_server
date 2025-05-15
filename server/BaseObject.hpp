#ifndef BASEOBJECT_HPP
#define BASEOBJECT_HPP
#include <atomic>
#include <liburing.h>

namespace gazellemq::server {
    static std::atomic<int> g_id{0};

    class BaseObject {
    protected:
        std::string id{};
    public:
        virtual ~BaseObject() = default;

        BaseObject() {
            id.append(std::to_string(g_id++));
        }
    public:
        void appendId(std::string const& val) {
            id.append(val);
        }

        void printId() const {
            printf("%s\n", id.c_str());
        }

        virtual void handleEvent (io_uring* ring, int res) = 0;
    };
}

#endif //BASEOBJECT_HPP
