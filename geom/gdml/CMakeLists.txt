############################################################################
# CMakeLists.txt file for building ROOT geom/gdml package
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(Gdml
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              DEPENDENCIES Geom XMLIO Hist RIO)

