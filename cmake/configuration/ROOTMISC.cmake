#-----------------------------------------------------------------------------------------------------
#---ROOT_PRINT_TUPLE_VALUE([var] )
#-----------------------------------------------------------------------------------------------------
function(ROOT_PRINT_TUPLE_VALUES tuplename)
  foreach(list_name IN LISTS tuplename)
    message(STATUS "For component " ${list_name} ":")
    foreach(value IN LISTS ${list_name})
      message(STATUS "-> defined next library: " ${value})
    endforeach()
  endforeach()
endfunction()

#-----------------------------------------------------------------------------------------------------
#---ROOT_GET_LIST_VALUE(tuplename package position)
#-----------------------------------------------------------------------------------------------------
function(ROOT_GET_LIST_NAME rootpackagemap module list_name)
  set(list_name_output ${list_name})
  foreach(listname IN LISTS rootpackagemap)
    foreach(value IN LISTS ${listname})
      #message(STATUS ${listname} " and " ${value})
      message(STATUS ${module} " = " ${value})
      if(${value} STREQUAL ${module})
        message(STATUS "We grepped next list " ${listname} " for the module " ${module})
        set(list_name_output ${listname})
      endif()
    endforeach()
  endforeach()
 set(${list_name} ${list_name_output} PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------------------------------
#---ROOT_CHECK_TUPLE_VALUE(tuplename name_of_module return_package)
#-----------------------------------------------------------------------------------------------------
function(ROOT_PARSE_TUPLE_VALUES list_name name_of_module return_package)x
  set(return_package_output ${return_package})
    foreach(value IN LISTS list_name)
      message(STATUS ${value} " is not " ${name_of_module} "?")
      if(${value} STREQUAL ${name_of_module})
        set(return_package_name ${value})
        message(STATUS "We got parced " ${value} " for package " ${return_package_output})
     endif()
  endforeach()
 set(${return_package} ${return_package_output} PARENT_SCOPE)
endfunction()
