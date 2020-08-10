#---Declare ZipZFP sources as part of libCore-------------------------------
set(headers ${CMAKE_CURRENT_SOURCE_DIR}/inc/ZipZFP.hxx)
set(sources ${CMAKE_CURRENT_SOURCE_DIR}/src/ZipZFP.cxx)

# FindZFP defines zfp::zfp and ${ZFP_INCLUDE_DIR}
ROOT_OBJECT_LIBRARY(Zfp ${sources})
add_dependencies(Zfp zfp::zfp)
target_include_directories(Zfp PRIVATE
	${ZFP_INCLUDE_DIR}
	${CMAKE_SOURCE_DIR}/core/base/inc
	${CMAKE_BINARY_DIR}/ginclude
)

ROOT_INSTALL_HEADERS()
install(FILES ${ZFP_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
