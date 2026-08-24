# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 4.2)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" FACEAUTH_VERSION LIMIT_COUNT 1)
string(STRIP "${FACEAUTH_VERSION}" FACEAUTH_VERSION)
if(NOT FACEAUTH_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION.txt must contain an x.y.z semantic version")
endif()
project(FaceAuthTPM VERSION ${FACEAUTH_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32)
  message(FATAL_ERROR "FaceAuthTPM is Windows-only")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Build x64 only")
endif()

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /guard:cf /sdl)
  add_link_options(/guard:cf)
endif()

option(FACEAUTH_TEST_ALLOW_REMOTE "TEST ONLY: allow FaceAuth in RDP/Hyper-V enhanced sessions" OFF)

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc highgui videoio dnn objdetect)

add_library(FaceAuthCommon STATIC
  src/common/Paths.cpp
  src/common/Security.cpp
  src/common/TpmVault.cpp
  src/common/FaceProfile.cpp
  src/common/IdentityRecord.cpp
  src/common/Cooldown.cpp
)
target_include_directories(FaceAuthCommon PUBLIC src)
target_link_libraries(FaceAuthCommon PUBLIC advapi32 bcrypt ncrypt shell32 secur32 wtsapi32)

add_library(FaceAuthVision STATIC src/vision/FaceEngine.cpp)
target_include_directories(FaceAuthVision PUBLIC src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(FaceAuthVision PUBLIC FaceAuthCommon ${OpenCV_LIBS})

add_executable(FaceAuthSensor src/sensor/main.cpp src/sensor/ModelClient.cpp)
target_include_directories(FaceAuthSensor PRIVATE src ${OpenCV_INCLUDE_DIRS})
# Deliberately do NOT link FaceAuthVision/opencv_dnn here. The sensor must reach
# camera.open() with the smallest possible DLL load set; inference lives in the
# preloaded service.
target_link_libraries(FaceAuthSensor PRIVATE FaceAuthCommon opencv_core opencv_videoio)

add_executable(FaceAuthCameraProbe src/cameraprobe/main.cpp)
target_include_directories(FaceAuthCameraProbe PRIVATE src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(FaceAuthCameraProbe PRIVATE FaceAuthCommon opencv_core opencv_videoio)

add_executable(FaceAuthModelService src/modelservice/main.cpp)
target_include_directories(FaceAuthModelService PRIVATE src)
target_link_libraries(FaceAuthModelService PRIVATE FaceAuthVision advapi32)

add_executable(FaceAuthEnroll src/enroll/main.cpp)
target_link_libraries(FaceAuthEnroll PRIVATE FaceAuthVision secur32)

add_executable(FaceAuthVaultProbe src/vaultprobe/main.cpp)
target_link_libraries(FaceAuthVaultProbe PRIVATE FaceAuthCommon)

add_library(FaceAuthCredentialProvider SHARED
  src/cp/FaceAuthCredentialProvider.def
  src/cp/FaceAuthCredentialProvider.rc
  src/cp/Dll.cpp
  src/cp/Provider.cpp
  src/cp/Credential.cpp
  src/cp/CpHelpers.cpp
)
target_include_directories(FaceAuthCredentialProvider PRIVATE src src/cp)
target_compile_definitions(FaceAuthCredentialProvider PRIVATE SECURITY_WIN32=1)
target_link_libraries(FaceAuthCredentialProvider PRIVATE
  FaceAuthCommon
  advapi32
  credui
  ole32
  propsys
  secur32
  shlwapi
  user32
  windowscodecs
  wtsapi32
)
if(FACEAUTH_TEST_ALLOW_REMOTE)
  target_compile_definitions(FaceAuthCredentialProvider PRIVATE FACEAUTH_TEST_ALLOW_REMOTE=1)
endif()
set_target_properties(FaceAuthCredentialProvider PROPERTIES OUTPUT_NAME "FaceAuthCredentialProvider")

install(TARGETS FaceAuthSensor FaceAuthCameraProbe FaceAuthModelService FaceAuthEnroll FaceAuthVaultProbe FaceAuthCredentialProvider RUNTIME DESTINATION . LIBRARY DESTINATION .)
