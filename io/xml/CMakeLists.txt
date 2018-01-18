############################################################################
# CMakeLists.txt file for building ROOT io/xml package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(XMLIO
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              DEPENDENCIES RIO)

