############################################################################
# CMakeLists.txt file for building ROOT io/sql package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(SQLIO
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              DEPENDENCIES Net RIO)
