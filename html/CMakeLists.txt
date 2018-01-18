############################################################################
# CMakeLists.txt file for building ROOT html package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(Html
                             DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                             DEPENDENCIES Graf Thread)

