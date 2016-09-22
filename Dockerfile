FROM ubuntu:latest

MAINTAINER Eric Dattore

RUN apt-get update \
    && apt-get -y install apt-utils \
    && apt-get -y install vim build-essential git gdb curl

EXPOSE 9873

