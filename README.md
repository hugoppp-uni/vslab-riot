# vslab-riot
Scaffold for practical exercise with RIOT, further information on lecture
[here](https://www.inet.haw-hamburg.de/teaching/ws-2019-20/verteilte-systeme-1).

## Introduction

This exercise is based on [RIOT-OS](https://riot-os.org), an open source OS for
the IoT. Its source code is on [Github](https://github.com/RIOT-OS/RIOT), more
Information can be found in the [RIOT-Wiki](https://github.com/RIOT-OS/RIOT/wiki),
there are [Tutorials](https://github.com/RIOT-OS/Tutorials), and the RIOT API
[documentation](https://doc.riot-os.org).

Please refer to the wiki giving a more detailed introduction, as well as
required information and hints on how to solve the exercise, see parts
[one](https://github.com/inetrg/vslab-riot/wiki/vslab-riot-part1) and
[two](https://github.com/inetrg/vslab-riot/wiki/vslab-riot-part2).

## Quickstart Guide

The following commands should run on most systems with any Linux and macOS.

```
git clone https://github.com/inetrg/vslab-riot.git
cd vslab-riot
git submodule update --init
make -C src clean all
```

## Problems?

Please don't hesitate to open an issue to report any bugs or problems related to source code and documentation. But don't ask for a solution to the exercise :)
