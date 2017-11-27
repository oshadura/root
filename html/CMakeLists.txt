############################################################################
# CMakeLists.txt file for building ROOT html package
############################################################################
if(NOT base)
   ROOT_STANDARD_LIBRARY_PACKAGE(Html DEPENDENCIES Graf Thread)
endif()
