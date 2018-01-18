############################################################################
# CMakeLists.txt file for building ROOT gui/sessionviewer package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(SessionViewer
                             DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                             DEPENDENCIES Proof Gui Hist Gpad Graf Tree MathCore)


