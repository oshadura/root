############################################################################
# CMakeLists.txt file for building ROOT math/foam package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(Foam
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              DEPENDENCIES Hist MathCore)

