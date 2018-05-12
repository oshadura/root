#!/usr/bin/env sh

set -ex

#creating installation directory
mkdir $HOME/installdir
mkdir ../builds
cd ../builds
#currently root compiles with GCC-6
cmake -DCMAKE_CC_COMPILER=/usr/bin/gcc-6 -DCMAKE_CXX_COMPILER=/usr/bin/g++-6 -DCMAKE_CXX_STANDARD="14" -DCMAKE_INSTALL_PREFIX="$HOME/installdir" -Dbuiltin_llvm="OFF" ../root
make
make DESTDIR=$HOME/installdir install
cd -
