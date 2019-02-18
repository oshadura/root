#----------------------------------------------------------------------------
# canonicalize_tool_name(name output)
#----------------------------------------------------------------------------
#
function(canonicalize_dir_name name output)
   string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" nameStrip ${name})
   string(REPLACE "-" "_" nameUNDERSCORE ${nameStrip})
   string(TOLOWER ${nameUNDERSCORE} nameLOWER)
   set(${output} "${nameLOWER}" PARENT_SCOPE)
endfunction(canonicalize_dir_name)

#----------------------------------------------------------------------------
# add_root_subdirectory(name type)
#----------------------------------------------------------------------------
# Custom add_subdirectory wrapper.
# Takes in the subdirectory name.
#
function(add_subdirectory name)
   # Make sure that we have a generic lower-case name of the module as an inputS
   canonicalize_dir_name(${name} nameLOWER)
   message(STATUS "We are doing to add subdirectory " ${name})
   if(root-lazy-build)
      # We check if the directory name belongs to modulemap and requested ROOT packages
      SET(return_value_check_modulemap OFF)
      check_modulemap(${name} return_value_check_modulemap)
      message(STATUS "--- Module " ${name} " -> " ${return_value_check_modulemap})
      # For LLVM directories, we add  directories without a check
      #FIXME: we dont need to recompile if we already have root-base ROOT Base package
      if("${PROJECT_NAME}" STREQUAL "LLVM")
         if(IS_ABSOLUTE ${name})
            if(EXISTS "${name}")
               message(STATUS "--- LLVM Module " ${name} " -> was added without check")
               _add_subdirectory(${name} ${ARGN})
            else()
               message(WARNING "--- Missing directory in add_subdirectory(${name})")
            endif()
         else()
            if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/CMakeLists.txt)
               message(STATUS "--- Module " ${name} " -> was added ")
               _add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${name} ${ARGN})
            else()
               message(WARNING "--- Missing directory in add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${name})")
            endif()
         endif()
      endif()
      # Adding directory after check if the directory name belongs to modulemap and requested ROOT packages
      if(return_value_check_modulemap)
         set(list_of_targets CACHE INTERNAL "")
         list(APPEND list_of_targets ${name})
         if(IS_ABSOLUTE ${name})
            if(EXISTS "${name}")
               message(STATUS "--- Module " ${name} " -> was added ")
               _add_subdirectory(${name} ${ARGN})
            else()
               message(WARNING "--- Missing directory in add_subdirectory(${name})")
            endif()
         else()
            if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/CMakeLists.txt)
               message(STATUS "--- Module " ${name} " -> was added ")
               _add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${name} ${ARGN})
            #Case for ROOT builtins
            # Particular case for error: When specifying an out-of-tree source a binary directory must be explicitly specified.
            elseif(EXISTS ${PROJECT_SOURCE_DIR}/${name}/CMakeLists.txt)
               _add_subdirectory(${PROJECT_SOURCE_DIR}/${name} ${name} ${ARGN})
            else()
               message(WARNING "--- Missing directory in add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${name})")
            endif()
         endif()
      endif()
   # Classical add_subdirectory() in case of old style ROOT build
   else()
      if(IS_ABSOLUTE ${name})
         if(EXISTS "${name}")
            _add_subdirectory(${name} ${ARGN})
         else()
            message(WARNING "--- Missing directory in add_subdirectory(${name})")
         endif()
      else()
         if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/CMakeLists.txt)
            _add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${name} ${ARGN})
         else()
            message(WARNING "--- Missing directory in add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${name})")
         endif()
      endif()
   endif()
endfunction()

#----------------------------------------------------------------------------
# check_modulemap(name return_value_check_modulemap)
#----------------------------------------------------------------------------
# Custom check_modulemap.
#
function(check_modulemap name return_value_check_modulemap)
   set(listname)
   SET_PROPERTY(GLOBAL PROPERTY listname "")
   if(root-lazy-build)
      foreach(list_name IN LISTS rootpackagemap_requested)
         #message(STATUS "Iterating through ==========> " ${list_name})
         foreach(value IN LISTS ${list_name})
            #message(STATUS ${value} " =? " ${name})
            if("${value}" STREQUAL "${name}")
               set(listname ${list_name})
               message(STATUS "******** We found module in package:: " ${listname} " ********")
            endif()
         endforeach()
      endforeach()
      foreach(opt ${root_packages})
         #message(STATUS "Enabled ROOT packages:: " ${opt})
         if("${opt}" STREQUAL "${listname}")
            set(${return_value_check_modulemap} ON PARENT_SCOPE)
            message(STATUS "--- [modulariz.] We found package " ${name} " from package " ${opt})
            break()
         else()
            set(${return_value_check_modulemap} OFF PARENT_SCOPE)
         endif()
      endforeach()
   endif()
endfunction()

#----------------------------------------------------------------------------
# install_moduleconfig(name)
#----------------------------------------------------------------------------
# Custom install_moduleconfig.
#
function(install_moduleconfig library)
   # Add all targets to the build-tree export set
   export(TARGETS ${library} FILE "${PROJECT_BINARY_DIR}/modules/${library}Targets.cmake")

   # Export the package for use from the build-tree
   # (this registers the build-tree with a global CMake-registry)
   export(PACKAGE ${library})

   # Create the FooBarConfig.cmake and FooBarConfigVersion files
   #file(RELATIVE_PATH REL_INCLUDE_DIR "${INSTALL_CMAKE_DIR}" "${INSTALL_INCLUDE_DIR}")
   # ... for the build tree
   set(CONF_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")
   configure_file(${library}Config.cmake.in
  "${PROJECT_BINARY_DIR}/${library}Config.cmake" @ONLY)
   # ... for the install tree
   set(CONF_INCLUDE_DIRS "\${FOOBAR_CMAKE_DIR}/${REL_INCLUDE_DIR}")
   configure_file(${library}Config.cmake.in
  "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${library}Config.cmake" @ONLY)
   # ... for both
   configure_file(${library}ConfigVersion.cmake.in
  "${PROJECT_BINARY_DIR}/${library}ConfigVersion.cmake" @ONLY)

   # Install the${library}Config.cmake and${library}ConfigVersion.cmake
   install(FILES
  "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${library}Config.cmake"
  "${PROJECT_BINARY_DIR}/${library}ConfigVersion.cmake"
  DESTINATION "${INSTALL_CMAKE_DIR}" COMPONENT dev)

   # Install the export set for use with the install-tree
   install(EXPORT ${library} DESTINATION
  "${INSTALL_CMAKE_DIR}" COMPONENT dev)
endfunction()
