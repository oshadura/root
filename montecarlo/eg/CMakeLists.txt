############################################################################
# CMakeLists.txt file for building ROOT montecarlo/eg package
# @author Pere Mato, CERN
############################################################################

ROOT_STANDARD_LIBRARY_PACKAGE(EG
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              HEADERS T*.h
                              DEPENDENCIES Graf3d Graf Gpad MathCore )
