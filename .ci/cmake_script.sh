#!/usr/bin/env bash

set -ex

#creating installation directory
mkdir $TRAVIS_BUILD_DIR/installdir
#mkdir ../builds
cd ../builds
if [[ -f touch_order.txt ]]; then
  while read fn; do
    touch $fn
  done < touch_order.txt
  # touch changed git files to trigger their rebuild
  read PREVIOUS_GIT_COMMIT < previous_git_commit.txt
  changed_files=`git diff --name-only $PREVIOUS_GIT_COMMIT HEAD`
  echo "Previously cached Travis build based on git commit ${PREVIOUS_GIT_COMMIT}."
  echo "... changed files since then:"
  echo $changed_files
  cd "${TRAVIS_BUILD_DIR}"
  touch `echo $changed_files`
  cd "${TRAVIS_BUILD_DIR}"/../builds
else
  cmake -DCMAKE_C_COMPILER=/usr/bin/gcc-6 -DCMAKE_CXX_COMPILER=/usr/bin/g++-6 -DCMAKE_CXX_STANDARD="14" -DCMAKE_INSTALL_PREFIX="$TRAVIS_BUILD_DIR/installdir" -Dbuiltin_llvm="OFF" ../root
fi
#currently root compiles with GCC-6
make -j4
make install
cd -
  


      
        
      
   
