# syntax=docker/dockerfile:1
FROM ubuntu:22.04

WORKDIR app

# install dependencies
RUN apt-get update
RUN apt-get install -y liburing-dev libjemalloc2

COPY cmake-build-release/gazellemq_server .

EXPOSE 5875

CMD ["./gazellemq_server"]


## make sure you build in release mode then you can build your docker image with the following
# docker build -t gazellemq:latest .
## then run your containerized gazellemq with the following
# docker run -p 127.0.0.1:5875:5875 gazellemq:latest
## or if the process keeps exiting try the following
# sudo docker run --entrypoint "/bin/sh"  -itd -p 127.0.0.1:5875:5875 dre767/gazellemq:latest