cmake_minimum_required(VERSION 3.25...4.4)   # 3.25+: "cmake -E env --modify" in the deploy step

project(SurveillanceSystem VERSION 1.0.0 LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Dependencies shipped in ./package (extracted to ./third_party by build_vs2019.bat)
# -----------------------------------------------------------------------------
set(SS_QT_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/package/qt/5.15.2/msvc2019_64" CACHE PATH "Qt 5.15.2 msvc2019_64 folder")
set(SS_OPENCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/build"     CACHE PATH "OpenCV build folder (contains OpenCVConfig.cmake)")
option(SS_USE_CUDA "Link the CUDA 12 build of ONNX Runtime (GPU inference)" OFF)
if(SS_USE_CUDA)
    set(SS_ORT_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime-win-x64-gpu_cuda12-1.30.0")
else()
    set(SS_ORT_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime-win-x64-1.30.0")
endif()
set(SS_ORT_DIR "" CACHE PATH "ONNX Runtime folder (empty = use the bundled CPU/GPU build per SS_USE_CUDA)")
if(NOT SS_ORT_DIR)
    set(ORT_ROOT "${SS_ORT_DEFAULT}")
else()
    set(ORT_ROOT "${SS_ORT_DIR}")
endif()

list(APPEND CMAKE_PREFIX_PATH "${SS_QT_DIR}")
set(OpenCV_DIR "${SS_OPENCV_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Svg Sql Concurrent)
find_package(OpenCV REQUIRED)

if(NOT EXISTS "${ORT_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Runtime not found in ${ORT_ROOT}. Run build_vs2019.bat once to extract the packages.")
endif()
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_IMPLIB "${ORT_ROOT}/lib/onnxruntime.lib"
    IMPORTED_LOCATION "${ORT_ROOT}/lib/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_ROOT}/include")

# -----------------------------------------------------------------------------
# Application
# -----------------------------------------------------------------------------
file(GLOB_RECURSE SS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(SurveillanceSystem WIN32
    ${SS_SOURCES}
    resources/resources.qrc
    resources/app.rc)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES ${SS_SOURCES})

target_include_directories(SurveillanceSystem PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SurveillanceSystem PRIVATE
    Qt5::Widgets Qt5::Svg Qt5::Sql Qt5::Concurrent
    ${OpenCV_LIBS}
    onnxruntime
    psapi)
target_compile_definitions(SurveillanceSystem PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES
    QT_DEPRECATED_WARNINGS)

if(MSVC)
    target_compile_options(SurveillanceSystem PRIVATE /MP /utf-8 /permissive- /Zc:__cplusplus /W3 /wd4251)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT SurveillanceSystem)
    set_target_properties(SurveillanceSystem PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:SurveillanceSystem>")
endif()

# -----------------------------------------------------------------------------
# Deployment: copy every runtime DLL next to the executable after each build,
# so the program runs straight from Visual Studio (F5) or the output folder.
# -----------------------------------------------------------------------------
string(REPLACE "." "" SS_OCV_VER "${OpenCV_VERSION}")
set(SS_ORT_DLLS "${ORT_ROOT}/lib/onnxruntime.dll" "${ORT_ROOT}/lib/onnxruntime_providers_shared.dll")
if(EXISTS "${ORT_ROOT}/lib/onnxruntime_providers_cuda.dll")
    list(APPEND SS_ORT_DLLS "${ORT_ROOT}/lib/onnxruntime_providers_cuda.dll")
endif()

add_custom_command(TARGET SurveillanceSystem POST_BUILD
    # Qt 5's windeployqt queries the first qmake on PATH: put ours in front.
    COMMAND ${CMAKE_COMMAND} -E env --modify "PATH=path_list_prepend:${SS_QT_DIR}/bin"
            "${SS_QT_DIR}/bin/windeployqt.exe"
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
            "$<TARGET_FILE:SurveillanceSystem>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:opencv_world>" "$<TARGET_FILE_DIR:SurveillanceSystem>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:opencv_world>/opencv_videoio_ffmpeg${SS_OCV_VER}_64.dll" "$<TARGET_FILE_DIR:SurveillanceSystem>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SS_ORT_DLLS} "$<TARGET_FILE_DIR:SurveillanceSystem>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SurveillanceSystem>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/models" "$<TARGET_FILE_DIR:SurveillanceSystem>/models"
    COMMENT "Deploying Qt, OpenCV and ONNX Runtime DLLs")

install(TARGETS SurveillanceSystem RUNTIME DESTINATION .)
