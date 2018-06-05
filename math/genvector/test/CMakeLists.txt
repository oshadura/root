project(genvector-tests)
find_package(ROOT REQUIRED)
include_directories(${ROOT_INCLUDE_DIRS})

set(Libraries Core RIO Net Hist Graf Graf3d Gpad Tree
              Rint Postscript Matrix Physics MathCore Thread GenVector)

set(TestGenVectorSource coordinates3D.cxx
                        coordinates4D.cxx
                        rotationApplication.cxx
                        stress3D.cxx
                        stress2D.cxx
                        testBoost.cxx
                        testGenVector.cxx
                        testIterator.cxx
                        testVectorIO.cxx
                        vectorOperation.cxx)

#---Build and add all the defined test in the list---------------
foreach(file ${TestGenVectorSource})
  get_filename_component(testname ${file} NAME_WE)
  ROOT_EXECUTABLE(${testname} ${file} LIBRARIES ${Libraries})
  ROOT_ADD_TEST(genetic-${testname} COMMAND ${testname})
endforeach()
