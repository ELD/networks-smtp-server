FROM ubuntu:xenial

MAINTAINER Eric Dattore

RUN apt-get update \
    && apt-get -y install apt-utils \
    && apt-get -y install vim build-essential git gdb curl

EXPOSE 10001

CMD ["/bin/bash"]

