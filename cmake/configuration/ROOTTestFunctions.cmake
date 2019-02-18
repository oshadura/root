#----------------------------------------------------------------------------
# ROOT_ADD_UNITTEST_DIR(<libraries ...>)
#----------------------------------------------------------------------------
function(ROOT_ADD_UNITTEST_DIR)
  ROOT_GLOB_FILES(test_files ${CMAKE_CURRENT_SOURCE_DIR}/*.cxx)
  # Get the component from the path. Eg. core to form coreTests test suite name.
  ROOT_PATH_TO_STRING(test_name ${CMAKE_CURRENT_SOURCE_DIR}/)
  ROOT_ADD_GTEST(${test_name}Unit ${test_files} LIBRARIES ${ARGN})
endfunction()

#----------------------------------------------------------------------------
# function ROOT_ADD_GTEST(<testsuite> source1 source2... LIBRARIES)
#
function(ROOT_ADD_GTEST test_suite)
  CMAKE_PARSE_ARGUMENTS(ARG "" "" "LIBRARIES" ${ARGN})
  include_directories(${CMAKE_CURRENT_BINARY_DIR} ${GTEST_INCLUDE_DIR} ${GMOCK_INCLUDE_DIR})

  ROOT_GET_SOURCES(source_files . ${ARG_UNPARSED_ARGUMENTS})
  # Note we cannot use ROOT_EXECUTABLE without user-specified set of LIBRARIES to link with.
  # The test suites should choose this in their specific CMakeLists.txt file.
  # FIXME: For better coherence we could restrict the libraries the test suite could link
  # against. For example, tests in Core should link only against libCore. This could be tricky
  # to implement because some ROOT components create more than one library.
  ROOT_EXECUTABLE(${test_suite} ${source_files} LIBRARIES ${ARG_LIBRARIES})
  target_link_libraries(${test_suite} gtest gtest_main gmock gmock_main)
  if(MSVC)
    set(test_exports "/EXPORT:_Init_thread_abort /EXPORT:_Init_thread_epoch
        /EXPORT:_Init_thread_footer /EXPORT:_Init_thread_header /EXPORT:_tls_index")
    set_property(TARGET ${test_suite} APPEND_STRING PROPERTY LINK_FLAGS ${test_exports})
  endif()

  ROOT_PATH_TO_STRING(mangled_name ${test_suite} PATH_SEPARATOR_REPLACEMENT "-")
  ROOT_ADD_TEST(gtest${mangled_name} COMMAND ${test_suite} WORKING_DIR ${CMAKE_CURRENT_BINARY_DIR})
endfunction()


#----------------------------------------------------------------------------
# ROOT_ADD_TEST_SUBDIRECTORY( <name> )
#----------------------------------------------------------------------------
function(ROOT_ADD_TEST_SUBDIRECTORY subdir)
  file(RELATIVE_PATH subdir ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${subdir})
  set_property(GLOBAL APPEND PROPERTY ROOT_TEST_SUBDIRS ${subdir})
endfunction()

#----------------------------------------------------------------------------
# ROOT_ADD_PYUNITTESTS( <name> )
#----------------------------------------------------------------------------
function(ROOT_ADD_PYUNITTESTS name)
  set(ROOT_ENV ROOTSYS=${ROOTSYS}
      PATH=${ROOTSYS}/bin:$ENV{PATH}
      LD_LIBRARY_PATH=${ROOTSYS}/lib:$ENV{LD_LIBRARY_PATH}
      PYTHONPATH=${ROOTSYS}/lib:$ENV{PYTHONPATH})
  string(REGEX REPLACE "[_]" "-" good_name "${name}")
  ROOT_ADD_TEST(pyunittests-${good_name}
                COMMAND ${PYTHON_EXECUTABLE} -B -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR} -p "*.py" -v
                ENVIRONMENT ${ROOT_ENV})
endfunction()

#----------------------------------------------------------------------------
# ROOT_ADD_PYUNITTEST( <name> <file>)
#----------------------------------------------------------------------------
function(ROOT_ADD_PYUNITTEST name file)
  CMAKE_PARSE_ARGUMENTS(ARG "WILLFAIL" "" "COPY_TO_BUILDDIR" ${ARGN})

  set(ROOT_ENV ROOTSYS=${ROOTSYS}
      PATH=${ROOTSYS}/bin:$ENV{PATH}
      LD_LIBRARY_PATH=${ROOTSYS}/lib:$ENV{LD_LIBRARY_PATH}
      PYTHONPATH=${ROOTSYS}/lib:$ENV{PYTHONPATH})
  string(REGEX REPLACE "[_]" "-" good_name "${name}")
  get_filename_component(file_name ${file} NAME)
  get_filename_component(file_dir ${file} DIRECTORY)

  if(ARG_COPY_TO_BUILDDIR)
    foreach(copy_file ${ARG_COPY_TO_BUILDDIR})
      get_filename_component(abs_path ${copy_file} ABSOLUTE)
      set(copy_files ${copy_files} ${abs_path})
    endforeach()
    set(copy_to_builddir COPY_TO_BUILDDIR ${copy_files})
  endif()

  if(ARG_WILLFAIL)
    set(will_fail WILLFAIL)
  endif()

  ROOT_ADD_TEST(pyunittests-${good_name}
                COMMAND ${PYTHON_EXECUTABLE} -B -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/${file_dir} -p ${file_name} -v
                ENVIRONMENT ${ROOT_ENV}
                ${copy_to_builddir}
                ${will_fail})
endfunction()
