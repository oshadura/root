############################################################################
# CMakeLists.txt file for building ROOT math/genvector package
############################################################################

set(headers32
  Math/Point2D.h
  Math/Point3D.h
  Math/Vector2D.h
  Math/Vector3D.h
  Math/Vector4D.h
)

set(headers
  Math/AxisAngle.h
  Math/Boost.h
  Math/BoostX.h
  Math/BoostY.h
  Math/BoostZ.h
  Math/EulerAngles.h
  Math/LorentzRotation.h
  Math/Plane3D.h
  Math/Point2D.h
  Math/Point3D.h
  Math/Quaternion.h
  Math/Rotation3D.h
  Math/RotationX.h
  Math/RotationY.h
  Math/RotationZ.h
  Math/RotationZYX.h
  Math/Transform3D.h
  Math/Translation3D.h
  Math/Vector2D.h
  Math/Vector3D.h
  Math/Vector4D.h
  Math/VectorUtil.h
)

set(sources
  src/3DConversions.cxx
  src/3DDistances.cxx
  src/AxisAngle.cxx
  src/AxisAngleXother.cxx
  src/BitReproducible.cxx
  src/Boost.cxx
  src/BoostX.cxx
  src/BoostY.cxx
  src/BoostZ.cxx
  src/EulerAngles.cxx
  src/GenVector_exception.cxx
  src/LorentzRotation.cxx
  src/Quaternion.cxx
  src/QuaternionXaxial.cxx
  src/Rotation3D.cxx
  src/Rotation3DxAxial.cxx
  src/RotationZYX.cxx
  src/VectorUtil.cxx
)

ROOT_STANDARD_LIBRARY_PACKAGE(GenVector
                              HEADERS ${headers}
                              SOURCES ${sources}
                              LINKDEF Math/LinkDef_GenVector.h
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              INSTALL_OPTIONS NO_INSTALL_HEADERS
                            DEPENDENCIES Core MathCore)

ROOT_STANDARD_LIBRARY_PACKAGE(GenVector32
                              HEADERS ${headers32}
                              SOURCES ${sources}
                              LINKDEF Math/LinkDef_GenVector32.h
                              DICTIONARY_OPTIONS "-writeEmptyRootPCM"
                              INSTALL_OPTIONS NO_INSTALL_HEADERS
                            DEPENDENCIES Core MathCore)

ROOT_ADD_TEST_SUBDIRECTORY(test)

ROOT_INSTALL_HEADERS()
