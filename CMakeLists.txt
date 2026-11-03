cmake_minimum_required(VERSION 3.10)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
project(rknn_mobilesam_demo)

if (NOT TARGET_SOC)
    set(TARGET_SOC rk3588)
endif()

if (ENABLE_ASAN)
    message(STATUS "BUILD WITH ADDRESS SANITIZER")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(RKNNRT_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/rknn_lib)
link_directories(${RKNNRT_LIB_PATH})

# 从 build 目录直接运行时加载项目内 librknnrt.so。
set(CMAKE_BUILD_RPATH "$ORIGIN/../rknn_lib")
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utils/ utils.out)

# MobileSAM 官方预处理和 Mask 后处理使用 OpenCV。
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "64bit")
    set(TARGET_LIB_ARCH lib64)
else()
    message(STATUS "32bit")
    set(TARGET_LIB_ARCH lib)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(OpenCV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/opencv/opencv-android-sdk-build/sdk/native/jni/abi-${CMAKE_ANDROID_ARCH_ABI})
else()
    if(TARGET_LIB_ARCH STREQUAL "lib")
        set(OpenCV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/opencv/opencv-linux-armhf/share/OpenCV)
    else()
        set(OpenCV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/opencv/opencv-linux-aarch64/share/OpenCV)
    endif()
endif()
find_package(OpenCV REQUIRED)
message(STATUS OpenCV_DIR=${OpenCV_DIR})
message(STATUS OpenCV_LIBS=${OpenCV_LIBS})

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

add_executable(${PROJECT_NAME}
    src/main.cc
    src/mobilesam.cc
    src/rknn_mobilesam_utils.cc
    src/preprocess.cc
    src/postprocess.cc
)

target_link_libraries(${PROJECT_NAME}
    imageutils
    fileutils
    imagedrawing
    rknnrt
    ${OpenCV_LIBS}
    dl
)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_link_libraries(${PROJECT_NAME} log)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRKNNRT_INCLUDES}
)

install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(FILES
    ${CMAKE_SOURCE_DIR}/model/coords.txt
    ${CMAKE_SOURCE_DIR}/model/labels.txt
    DESTINATION model
)
file(GLOB RKNN_FILES "${CMAKE_SOURCE_DIR}/model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model)
install(FILES ${RKNNRT_LIB_PATH}/librknnrt.so DESTINATION lib)
