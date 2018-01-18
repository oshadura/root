############################################################################
# CMakeLists.txt file for building ROOT math/fumili package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(Fumili
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              DEPENDENCIES Graf Hist MathCore)


