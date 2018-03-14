add_root_subdirectory (geom  ALL)        # special CMakeLists.txt
add_root_subdirectory (geombuilder AL) # special CMakeLists.txt
add_root_subdirectory (geompainter ALL) # special CMakeLists.txt
if(gdml)
  add_root_subdirectory(gdml ALL)
  if(NOT gnuinstall)
    install(DIRECTORY gdml/ DESTINATION geom/gdml
            FILES_MATCHING PATTERN "*.py"
            PATTERN "inc" EXCLUDE
            PATTERN "src" EXCLUDE)
  endif()
endif()
if(geocad)
  add_root_subdirectory(geocad ALL)
endif()
if(vecgeom)
  add_root_subdirectory(vecgeom ALL)
endif()
  

