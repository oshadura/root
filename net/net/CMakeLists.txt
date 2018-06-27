############################################################################
# CMakeLists.txt file for building ROOT net/net package
############################################################################

#Removing global headers and sources
#ROOT_GLOB_HEADERS(headers RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/inc inc/*.h)
#ROOT_GLOB_SOURCES(sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/src src/*.cxx)

set(headers RRemoteProtocol.h
 TGridJDL.h
 TPSocket.h
 TSQLMonitoring.h
 TWebFile.h
 TNetFileStager.h
 TGrid.h
 TSQLColumnInfo.h
 TSecContext.h
 TGridCollection.h
 NetErrors.h
 TGridResult.h
 TS3HTTPRequest.h
 TSQLStatement.h
 TNetFile.h
 TSQLServer.h
 TPServerSocket.h
 TGridJobStatusList.h
 TSSLSocket.h
 TUDPSocket.h
 TServerSocket.h
 TSQLResult.h
 TFileStager.h
 TGridJob.h
 TMonitor.h
 TApplicationRemote.h
 TParallelMergingFile.h
 TSQLTableInfo.h
 TS3WebFile.h
 TFTP.h
 TSQLRow.h
 TApplicationServer.h
 TGridJobStatus.h
 TSocket.h
 TMessage.h
)

set(sources src/TMessage.cxx
 src/TFTP.cxx
 src/TS3HTTPRequest.cxx
 src/TGridJobStatus.cxx
 src/TSQLMonitoring.cxx
 src/TNetFileStager.cxx
 src/TGridResult.cxx
 src/TSSLSocket.cxx
 src/TPServerSocket.cxx
 src/TFileStager.cxx
 src/TMonitor.cxx
 src/TGridJobStatusList.cxx
 src/TSQLResult.cxx
 src/TUDPSocket.cxx
 src/TPSocket.cxx
 src/TSQLTableInfo.cxx
 src/TApplicationServer.cxx
 src/TSecContext.cxx
 src/TParallelMergingFile.cxx
 src/TSQLStatement.cxx
 src/NetErrors.cxx
 src/TS3WebFile.cxx
 src/TGridJDL.cxx
 src/TGrid.cxx
 src/TApplicationRemote.cxx
 src/TSQLColumnInfo.cxx
 src/TGridJob.cxx
 src/TServerSocket.cxx
 src/TNetFile.cxx
 src/TSocket.cxx
 src/TSQLRow.cxx
 src/TWebFile.cxx
 src/TSQLServer.cxx)

if(NOT ssl)
  list(REMOVE_ITEM headers TSSLSocket.h)
  list(REMOVE_ITEM sources TSSLSocket.cxx)
  set(ssllib)
else()
  set(ssllib ${OPENSSL_LIBRARIES} ${CMAKE_DL_LIBS})
  add_definitions(-DR__SSL)
  include_directories(${OPENSSL_INCLUDE_DIR})
endif()

if(NOT ssl OR NOT CRYPTLIBS)
  list(REMOVE_ITEM headers TS3HTTPRequest.h)
  list(REMOVE_ITEM headers TS3WebFile.h)
  list(REMOVE_ITEM sources TS3HTTPRequest.cxx)
  list(REMOVE_ITEM sources TS3WebFile.cxx)
  add_definitions(-DR__NO_CRYPTO)
endif()

if(MACOSX_SSL_DEPRECATED)
  set_source_files_properties(src/TSSLSocket.cxx COMPILE_FLAGS "-Wno-deprecated-declarations" )
endif()

ROOT_STANDARD_LIBRARY_PACKAGE(Net
                              HEADERS ${headers}
                              SOURCES ${sources}
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              LIBRARIES ${ssllib} ${CRYPTLIBS}
                              DEPENDENCIES RIO BUILTINS OPENSSL)
