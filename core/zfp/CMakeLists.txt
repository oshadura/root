find_library(ZFP zfp)

if(NOT ZFP)
	message(FATAL_ERROR "could not find library zfp")
endif()		

#---Declare ZipZFP sources as part of libCore-------------------------------
set(headers ${CMAKE_CURRENT_SOURCE_DIR}/inc/ZipZFP.hxx)
set(sources ${CMAKE_CURRENT_SOURCE_DIR}/src/ZipZFP.cxx)

find_path(R_ZFP_INCLUDE_DIR zfp.h)

ROOT_OBJECT_LIBRARY(R_zfp ${sources} BUILTINS ZFP)
target_compile_definitions(R_zfp PRIVATE ${R_ZFP_DEFINITIONS})
target_include_directories(R_zfp PRIVATE
		${R_ZFP_INCLUDE_DIR}
   ${CMAKE_SOURCE_DIR}/core/base/inc
   ${CMAKE_BINARY_DIR}/ginclude
)

ROOT_INSTALL_HEADERS()
install(FILES ${R_ZFP_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
