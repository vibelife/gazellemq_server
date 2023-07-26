cmake_minimum_required(VERSION 3.24)
project(gazellemq_server)

set(CMAKE_CXX_STANDARD 20)

add_executable(${PROJECT_NAME} main.cpp lib/MPMCQueue/MPMCQueue.hpp server/Hub.hpp server/MessageSubscriber.hpp server/MessagePublisher.hpp server/ClientConnection.hpp server/Consts.hpp server/EventLoopObject.hpp server/ServerConnection.hpp server/Enums.hpp server/MessageHandler.hpp)

find_package(PkgConfig REQUIRED)

find_package(Threads)
link_libraries(${Threads})

find_library(ANL anl)
link_libraries(${ANL})

pkg_check_modules(URING liburing)
find_library(URING uring)
link_libraries(${URING})

target_link_libraries(${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT} ${URING} ${ANL})
