find_package(Snappy REQUIRED)

ROOT_OBJECT_LIBRARY(Snappy src/ZipSnappy.cxx)
target_include_directories(Snappy PRIVATE ${SNAPPY_INCLUDE_DIR})

ROOT_INSTALL_HEADERS()
