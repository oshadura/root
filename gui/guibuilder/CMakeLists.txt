############################################################################
# CMakeLists.txt file for building ROOT gui/guibuilder package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(GuiBld
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              DEPENDENCIES Gui Graf MathCore)

