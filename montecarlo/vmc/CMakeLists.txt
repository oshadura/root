############################################################################
# CMakeLists.txt file for building ROOT montecarlo/vmc package
# @author Pere Mato, CERN
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(VMC
                             DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                             DEPENDENCIES EG Geom MathCore)
