add_root_subdirectory(hist ALL)             # special CMakeLists.txt
add_root_subdirectory(histdraw ALL)         # special CMakeLists.txt
add_root_subdirectory(histpainter ALL)      # special CMakeLists.txt
add_root_subdirectory(spectrum ALL)
add_root_subdirectory(spectrumpainter ALL)  # special CMakeLists.txt
if(xml)
  add_root_subdirectory(unfold ALL)
endif()
if(CMAKE_Fortran_COMPILER)
  add_root_subdirectory(hbook ALL)
endif()

