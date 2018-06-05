############################################################################
# CMakeLists.txt file for building ROOT math/smatrix package
############################################################################

set(headers Math/SMatrix.h Math/SVector.h Math/SMatrixDfwd.h Math/SMatrixFfwd.h)

ROOT_STANDARD_LIBRARY_PACKAGE(Smatrix
                              HEADERS ${headers}
                              NO_SOURCES
                              LINKDEF LinkDef.h
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              INSTALL_OPTIONS NO_INSTALL_HEADERS
                            DEPENDENCIES Core MathCore Matrix)

ROOT_STANDARD_LIBRARY_PACKAGE(Smatrix32
                              HEADERS ${headers}
                              NO_SOURCES
                              LINKDEF LinkDefD32.h
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              INSTALL_OPTIONS NO_INSTALL_HEADERS
                            DEPENDENCIES Core MathCore Matrix)

ROOT_INSTALL_HEADERS()
