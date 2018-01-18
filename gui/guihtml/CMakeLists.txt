############################################################################
# CMakeLists.txt file for building ROOT gui/guihtml package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(GuiHtml
                             DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                             DEPENDENCIES Gui Graf Net)

