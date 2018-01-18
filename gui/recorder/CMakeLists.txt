############################################################################
# CMakeLists.txt file for building ROOT gui/recorder package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(Recorder
                             DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                             DEPENDENCIES RIO Tree Gui Thread)

