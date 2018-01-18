############################################################################
# CMakeLists.txt file for building ROOT math/physics package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(MLP
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              DEPENDENCIES Hist Matrix Tree Graf Gpad TreePlayer MathCore)

