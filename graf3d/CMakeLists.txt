add_root_subdirectory(g3d ALL) # special CMakeLists.txt
if(NOT WIN32 AND x11)
  add_root_subdirectory(x3d ALL) # special CMakeLists.txt
endif()
if (opengl)
   add_root_subdirectory(eve ALL) # special CMakeLists.txt
   add_root_subdirectory(gl ALL) # special CMakeLists.txt
   if(builtin_glew)
      add_root_subdirectory(glew ALL)
   endif()
   if(builtin_ftgl)
      add_root_subdirectory(ftgl ALL)
   endif()
  add_root_subdirectory(gviz3d ALL) # special CMakeLists.txt
endif()

