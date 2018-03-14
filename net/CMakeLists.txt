add_root_subdirectory(net ALL)
add_root_subdirectory(auth ALL)  # special CMakeListst.txt

if(bonjour)
  add_root_subdirectory(bonjour ALL)
endif()

if(krb5)
  add_root_subdirectory(krb5auth ALL)
endif()

if(NOT WIN32 AND ssl)
  add_root_subdirectory(rpdutils ALL)
  add_root_subdirectory(rootd ALL)
endif()

if(xrootd)
  add_root_subdirectory(netx ALL)
endif()

if(alien)
  add_root_subdirectory(alien ALL)
endif()

if(monalisa)
  add_root_subdirectory(monalisa ALL)
endif()

if(ldap)
   add_root_subdirectory(ldap ALL)
endif()

if(globus)
   add_root_subdirectory(globusauth ALL)
endif()

if(davix)
   add_root_subdirectory(davix ALL)
endif()

if(netxng)
   add_root_subdirectory(netxng ALL)
endif()

if(http)
	add_subdirectory(http ALL)
	add_subdirectory(httpsniff ALL)
endif()

