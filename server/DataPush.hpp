#ifndef GAZELLEMQ_SERVER_DATAPUSH_HPP
#define GAZELLEMQ_SERVER_DATAPUSH_HPP

namespace gazellemq::server {
    class DataPush: public EventLoopObject {
    private:
        enum DataPushState {
            DataPushState_notSet,
            DataPushState_sendData,
            DataPushState_done,
        };

        DataPushState state{DataPushState_notSet};
    public:
        int fd;
        std::string writeBuffer;

        [[nodiscard]] bool isDone() const {
            return state == DataPushState_done;
        }

    public:
        void handleEvent(struct io_uring *ring, int res) override {
            switch (state) {
                case DataPushState_notSet:
                    sendData(ring);
                    break;
                case DataPushState_sendData:
                    onSendDataComplete(ring, res);
                    break;
                case DataPushState_done:
                    break;
            }
        }
    private:
        /**
         * Sends the remaining bytes to the endpoint
         * @param ring
         */
        void sendData(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, writeBuffer.c_str(), writeBuffer.size(), 0);

            state = DataPushState_sendData;
            io_uring_sqe_set_data(sqe, this);
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done sending the bytes
         * @param ring
         * @param res
         * @return
         */
        bool onSendDataComplete(struct io_uring *ring, int res) {
            if (res > -1) {
                writeBuffer.erase(0, res);
                if (writeBuffer.empty()) {
                    // To get here means we've sent all the data
                    state = DataPushState_done;
                } else {
                    sendData(ring);
                }
            } else {
                printError("onSendDataComplete", res);
            }
        }

    };
}

#endif //GAZELLEMQ_SERVER_DATAPUSH_HPP
