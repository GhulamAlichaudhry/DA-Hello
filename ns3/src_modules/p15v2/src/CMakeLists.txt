# SPDX-License-Identifier: GPL-2.0-only
# contrib/dahello — Bellhop channel lookup for Aqua-Sim / ns-3.
# Kept out of src/aqua-sim-ng so the loader is removable without
# patching Aqua-Sim itself.

build_lib(
  LIBNAME dahello
  SOURCE_FILES
    model/bellhop-data-loader.cc
    model/channel-gate.cc
    model/link-quality.cc
    model/lqarp-protocol.cc
    model/dahello-protocol.cc
    model/rlor-inspired-protocol.cc
    model/qelar-protocol.cc
    model/saqr-protocol.cc
    model/marv-protocol.cc
    model/gauss-markov.cc
    model/dahello-run-config.cc
  HEADER_FILES
    model/bellhop-data-loader.h
    model/channel-gate.h
    model/link-quality.h
    model/lqarp-protocol.h
    model/dahello-protocol.h
    model/rlor-inspired-protocol.h
    model/qelar-protocol.h
    model/saqr-protocol.h
    model/marv-protocol.h
    model/gauss-markov.h
    model/dahello-run-config.h
    model/acoustic-energy.h
  LIBRARIES_TO_LINK
    ${libcore}
)

build_lib_example(
  NAME bellhop-data-loader-check
  SOURCE_FILES examples/bellhop-data-loader-check.cc
  LIBRARIES_TO_LINK
    ${libdahello}
    ${libcore}
)

build_lib_example(
  NAME lqarp-unit-check
  SOURCE_FILES examples/lqarp-unit-check.cc
  LIBRARIES_TO_LINK
    ${libdahello}
    ${libcore}
)

build_lib_example(
  NAME scenario-i-lqarp
  SOURCE_FILES examples/scenario-i-lqarp.cc
  LIBRARIES_TO_LINK
    ${libdahello}
    ${libcore}
    ${libnetwork}
    ${libmobility}
)
