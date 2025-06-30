cmake_minimum_required(VERSION 3.22)
project(gazellemq_server)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

add_executable(${PROJECT_NAME} main.cpp lib/MPMCQueue/MPMCQueue.hpp server/Consts.hpp server/Enums.hpp server/StringUtils.hpp server/MessageQueue.hpp server/MessageBatch.hpp
        server/ServerContext.hpp
        server/BaseObject.hpp
        server/PubSubHandler.hpp
        server/publisher/TCPPublisherHandler.hpp
        server/subscriber/TCPSubscriberHandler.hpp
        server/BaseServer.hpp
        server/subscriber/SubscriberServer.hpp
        server/publisher/PublisherServer.hpp
        server/command/CommandServer.hpp
        server/command/CommandHandler.hpp
        server/subscriber/WSSubscriberHandler.hpp
        server/publisher/WSPublisherHandler.hpp
        server/TimeUtils.hpp)

find_package(PkgConfig REQUIRED)

find_package(Threads)
link_libraries(${Threads})

find_library(ANL anl)
link_libraries(${ANL})

pkg_check_modules(URING liburing)
find_library(URING uring)
link_libraries(${URING})

pkg_check_modules (JEMALLOC jemalloc)

pkg_search_module(JEMALLOC REQUIRED jemalloc)
include_directories(${JEMALLOC_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} ${JEMALLOC_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${URING} ${ANL})
