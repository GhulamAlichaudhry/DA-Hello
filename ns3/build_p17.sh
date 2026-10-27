#!/bin/bash
# Usage: build.sh <variant: base|instr>
# Out-of-tree build of the dahello module + scenario binary with the SAME flags as the
# ns-3 tree (cmake-cache/compile_commands.json), linked against the existing ns-3 core libs.
set -euo pipefail
V=$1
A=${WORKSPACE}/DA-HELLO-porta
NS=${WORKSPACE}/DA-HELLO/opt/ns-allinone-3.41/ns-3.41
SRC=$A/build_p17/src
INC=$A/build_p17/include_p17
OBJ=$A/build_p17/obj_p17
LIB=$A/build_p17/lib_p17
BIN=$A/build_p17/bin_p17
mkdir -p $INC/ns3 $OBJ $LIB $BIN
for h in $SRC/model/*.h; do echo "#include \"$h\"" > $INC/ns3/$(basename $h); done
DEFS="-DNS3_BUILD_PROFILE_OPTIMIZED -DPROJECT_SOURCE_PATH=\"$NS\" -D__LINUX__"
FLAGS="${EXTRAFLAGS:-} -O3 -DNDEBUG -std=c++20 -fno-semantic-interposition -fdiagnostics-color=never -Wall -march=native -mtune=native"
objs=()
ORDER="acoustic-phy acoustic-medium bellhop-data-loader channel-gate link-quality lqarp-protocol dahello-protocol rlor-inspired-protocol qelar-protocol saqr-protocol marv-protocol gauss-markov dahello-run-config"
[ -f $SRC/model/dahello-audit.cc ] && ORDER="$ORDER dahello-audit"
for b in $ORDER; do
  cc=$SRC/model/$b.cc
  o=$OBJ/$(basename $cc).o
  c++ $DEFS -I$INC -I$NS/build/include $FLAGS -fPIC -o $o -c $cc &
  objs+=($o)
done
c++ $DEFS -I$INC -I$NS/build/include $FLAGS -fPIE -o $OBJ/scenario-i-lqarp.cc.o -c $SRC/examples/scenario-i-lqarp.cc &
wait
c++ -O3 -DNDEBUG -fPIC -shared -Wl,-soname,libns3.41-dahello-optimized.so -o $LIB/libns3.41-dahello-optimized.so "${objs[@]}" \
  -Wl,-rpath,$NS/build/lib -Wl,--no-as-needed $NS/build/lib/libns3.41-core-optimized.so -Wl,--as-needed
c++ ${EXTRAFLAGS:-} -O3 -DNDEBUG $OBJ/scenario-i-lqarp.cc.o -o $BIN/ns3.41-scenario-i-lqarp-optimized \
  -Wl,-rpath,$LIB:$NS/build/lib -Wl,--no-as-needed $LIB/libns3.41-dahello-optimized.so \
  $NS/build/lib/libns3.41-mobility-optimized.so -Wl,--as-needed $NS/build/lib/libns3.41-network-optimized.so \
  $NS/build/lib/libns3.41-stats-optimized.so $NS/build/lib/libns3.41-core-optimized.so -Wl,--no-as-needed -Wl,--as-needed
ldd $BIN/ns3.41-scenario-i-lqarp-optimized | grep dahello
echo BUILD_OK $V
# Port A unit tests (medium/PHY)
c++ $DEFS -I$INC -I$NS/build/include $FLAGS -fPIE -o $OBJ/medium-unit-check.cc.o -c $SRC/examples/medium-unit-check.cc
c++ -O3 -DNDEBUG $OBJ/medium-unit-check.cc.o -o $BIN/medium-unit-check \
  -Wl,-rpath,$LIB:$NS/build/lib -Wl,--no-as-needed $LIB/libns3.41-dahello-optimized.so $NS/build/lib/libns3.41-core-optimized.so
echo BUILD_TESTS_OK $V
# p3: eval-seed guard unit check (pure logic, no ns-3, no simulation)
c++ -O2 -std=c++20 -Wall -I$SRC/examples -o $BIN/eval-guard-unit-check $SRC/examples/eval-guard-unit-check.cc
echo BUILD_EVALGUARD_OK $V
# p6: LQARP position-aware DA test unit check (pure function; links the model lib for LQARP statics)
c++ $DEFS -I$INC -I$NS/build/include $FLAGS -fPIE -o $OBJ/lqarp-posda-unit-check.cc.o -c $SRC/examples/lqarp-posda-unit-check.cc
c++ -O3 -DNDEBUG $OBJ/lqarp-posda-unit-check.cc.o -o $BIN/lqarp-posda-unit-check \
  -Wl,-rpath,$LIB:$NS/build/lib -Wl,--no-as-needed $LIB/libns3.41-dahello-optimized.so $NS/build/lib/libns3.41-core-optimized.so
echo BUILD_POSDA_OK $V
# p9: comparator logic unit check (pure, no ns-3)
c++ -O2 -std=c++20 -Wall -I$SRC/examples -o $BIN/comparators-unit-check $SRC/examples/comparators-unit-check.cc
echo BUILD_CMP_OK $V
