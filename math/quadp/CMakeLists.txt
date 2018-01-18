############################################################################
# CMakeLists.txt file for building ROOT math/physics package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(Quadp
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              DEPENDENCIES Matrix)
