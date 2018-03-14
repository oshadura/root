add_root_subdirectory(proof  ALL)
add_root_subdirectory(proofplayer ALL) # special CMakeListst.txt
if(NOT WIN32)
  add_root_subdirectory(proofbench ALL) # special CMakeListst.txt
endif()
if(afdsmgrd)
  add_root_subdirectory(afdsmgrd ALL)
endif()
if(xrootd AND ssl)
  add_root_subdirectory(proofd ALL)
  add_root_subdirectory(proofx ALL)
endif()
add_root_subdirectory(pq2 ALL)
