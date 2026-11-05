# The Klipper printer's board: Klipper's AVR firmware running in simulavr.
#
# This is prind's build-simulavr stage (docker/klipper/Dockerfile at
# github.com/mkuf/prind 6f7690117230f4128101f0b0d2d72bf8a37cae8a), changed
# where prind's file has no switch:
# - It starts from prind's published tools image for the klipper service's
#   tag, which holds that tag's Klipper source, so the firmware always matches
#   the Klipper that talks to it.
# - simulavr is built with -O2 (as prind's README suggests) and the 16 MHz
#   board is paced to half of real time (avrsim.py -r 0.5). Unpaced, simulavr
#   runs the board as fast as one core allows: prind's debug build managed
#   about a fifth of 16 MHz (prints took five times their estimate), and an
#   -O2 build just under 16 MHz, where it fell behind on busy moves and
#   Klipper shut down with "Timer too close". An 8 MHz board paced to real
#   time shut down the same way. Klipper keeps time with a board that runs
#   slow but steady, so at half speed prints take twice their estimate.
# config.simulavr is prind's file of the same name.
ARG KLIPPER_TAG
FROM mkuf/klipper:${KLIPPER_TAG}-tools

RUN apt update \
 && apt install -y git cmake swig python3-dev python3-setuptools \
 && apt clean

WORKDIR /opt/klipper
COPY config.simulavr .config
# olddefconfig fills in options newer than config.simulavr (such as
# CONFIG_AVR_BUILD_MCU). make alone skips it when the copied file looks newer
# than Klipper's Kconfig files, and the build then fails on an empty -mmcu=.
RUN make olddefconfig \
 && make

WORKDIR /opt
RUN mkdir run \
 && groupadd simulavr --gid 1000 \
 && useradd simulavr --uid 1000 --gid simulavr \
 && usermod simulavr --append --groups dialout

ENV CXXFLAGS="-O2"
RUN git clone git://git.savannah.nongnu.org/simulavr.git \
 && cd simulavr \
 && git checkout release-1.1.0 \
 && make python \
 && make build \
 && chown -R simulavr:simulavr /opt/simulavr

USER simulavr
ENV PYTHONPATH=/opt/simulavr/build/pysimulavr/
VOLUME ["/opt/printer_data/run"]
ENTRYPOINT ["klipper/scripts/avrsim.py"]
CMD ["-r", "0.5", "-p", "/opt/printer_data/run/simulavr.tty", "/opt/klipper/out/klipper.elf"]
