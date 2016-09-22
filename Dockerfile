FROM ubuntu:latest

MAINTAINER Eric Dattore

ENV LANG en_US.UTF-8
ENV LANGUAGE en_US:en
ENV LC_ALL en_US.UTF-8

RUN apt-get update \
    && apt-get -y install build-essential git gdb curl

VOLUME ["/Work"]

EXPOSE 9873

ADD . /Work

