# Copyright (C) 1995-2020, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

############################################################################
# CMakeLists.txt file for building ROOT ntuple lite package
# @author Oksana Shadura CERN
############################################################################

if(NOT root7)
  return()
endif()

set(ROOTNTUPLELITE_SOURCES
  src/NTupleLite.cpp
)

ROOT_LINKER_LIBRARY(ROOTNTupleLite ${ROOTNTUPLELITE_SOURCES} DEPENDENCIES ROOTFoundationIOLite)

ROOT_INSTALL_HEADERS()
