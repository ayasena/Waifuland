cmake_minimum_required(VERSION 3.16)

# Set app name.
set(APP_NAME waifuland)

# Set directory paths based on the new layout
set(SDK_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CubismSdkForNative-5-r.5)
set(CORE_PATH ${SDK_ROOT_PATH}/Core)
set(FRAMEWORK_PATH ${SDK_ROOT_PATH}/Framework)
set(THIRD_PARTY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty)

set(STB_PATH ${THIRD_PARTY_PATH}/stb)
set(GLEW_PATH ${THIRD_PARTY_PATH}/glew)

# Shaders from SDK
set(SAMPLE_SHADER_PATH ${SDK_ROOT_PATH}/Samples/OpenGL/Shaders/Standard)
set(FRAMEWORK_SHADER_PATH ${FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard)
set(COMMON_SRC_PATH ${SDK_ROOT_PATH}/Samples/Common)

# Set project.
project(${APP_NAME})

if (APPLE)
  enable_language(OBJCXX)
endif ()

# Define output directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
# Set configuration (Release and Debug only).
set(CMAKE_CONFIGURATION_TYPES Debug Release
  CACHE STRING "Configurations" FORCE
)
# Suppress generation of ZERO_CHECK project.
set(CMAKE_SUPPRESS_REGENERATION ON)

# Suppress GLEW process. macOS uses CGL/NSOpenGL, not EGL.
if (NOT APPLE)
  set(GLEW_EGL ON CACHE BOOL "" FORCE)
endif ()
set(BUILD_UTILS OFF CACHE BOOL "" FORCE)

# Specify version of compiler.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add Cubism Core.
add_library(Live2DCubismCore STATIC IMPORTED)
if (APPLE)
  # The macOS SDK ships separate arm64/x86_64 static libs (no universal binary),
  # under Core/lib/macos/<arch>/.
  if (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(CUBISM_CORE_MAC_ARCH "arm64")
  else ()
    set(CUBISM_CORE_MAC_ARCH "x86_64")
  endif ()
  set_target_properties(Live2DCubismCore
    PROPERTIES
      IMPORTED_LOCATION ${CORE_PATH}/lib/macos/${CUBISM_CORE_MAC_ARCH}/libLive2DCubismCore.a
      INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
  )
else ()
  # The SDK only ships a stable lib for linux/x86_64; arm64 (e.g. Raspberry Pi,
  # ARM servers) is under Core/lib/experimental/linux/arm64 upstream.
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CUBISM_CORE_LINUX_LIB ${CORE_PATH}/lib/experimental/linux/arm64/libLive2DCubismCore.a)
  else ()
    set(CUBISM_CORE_LINUX_LIB ${CORE_PATH}/lib/linux/x86_64/libLive2DCubismCore.a)
  endif ()
  set_target_properties(Live2DCubismCore
    PROPERTIES
      IMPORTED_LOCATION ${CUBISM_CORE_LINUX_LIB}
      INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
  )
endif ()

# Find opengl libraries. EGL only exists on Linux; macOS uses the system OpenGL framework directly.
if (APPLE)
  find_package(OpenGL REQUIRED COMPONENTS OpenGL)
else ()
  find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
endif ()

# Add GLEW.
add_subdirectory(${GLEW_PATH}/build/cmake ${CMAKE_CURRENT_BINARY_DIR}/glew)

# Specify Cubism Framework rendering.
set(FRAMEWORK_SOURCE OpenGL)
# Add Cubism Native Framework.
add_subdirectory(${FRAMEWORK_PATH} ${CMAKE_CURRENT_BINARY_DIR}/Framework)
# Add rendering definition to framework.
if (APPLE)
  target_compile_definitions(Framework PUBLIC CSM_TARGET_MAC_GL)
else ()
  target_compile_definitions(Framework PUBLIC CSM_TARGET_LINUX_GL)
endif ()
# Add include path of GLEW to framework.
target_include_directories(Framework PUBLIC ${GLEW_PATH}/include)
# Link libraries to framework.
target_link_libraries(Framework Live2DCubismCore glew_s)

if (NOT APPLE)
  # Find Wayland & EGL via PkgConfig
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl wayland-cursor egl)
  pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

  # Generate Wayland protocol files
  set(PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protocol)
  set(WAYLAND_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  file(MAKE_DIRECTORY ${WAYLAND_GEN_DIR})

  macro(wayland_generate_protocol PROTOCOL_XML NAMESPACE)
      set(HEADER "${WAYLAND_GEN_DIR}/${NAMESPACE}-client-protocol.h")
      set(CODE "${WAYLAND_GEN_DIR}/${NAMESPACE}-protocol.c")

      add_custom_command(
          OUTPUT ${HEADER}
          COMMAND ${WAYLAND_SCANNER} client-header ${PROTOCOL_XML} ${HEADER}
          DEPENDS ${PROTOCOL_XML}
      )
      add_custom_command(
          OUTPUT ${CODE}
          COMMAND ${WAYLAND_SCANNER} private-code ${PROTOCOL_XML} ${CODE}
          DEPENDS ${PROTOCOL_XML} ${HEADER}
      )
      list(APPEND PROTOCOL_SOURCES ${CODE} ${HEADER})
  endmacro()

  wayland_generate_protocol(${PROTOCOL_DIR}/xdg-shell.xml xdg-shell)
  wayland_generate_protocol(${PROTOCOL_DIR}/wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1)
endif ()

# Make executable app.
add_executable(${APP_NAME} ${PROTOCOL_SOURCES})
# Add common source files from SDK.
add_subdirectory(${COMMON_SRC_PATH} ${CMAKE_CURRENT_BINARY_DIR}/src/Common)
# Add our source files in ./src
add_subdirectory(src)

# Link libraries to app.
if (APPLE)
  find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
  find_library(APPSERVICES_FRAMEWORK ApplicationServices REQUIRED)
  target_link_libraries(${APP_NAME}
    Framework
    ${OPENGL_LIBRARIES}
    ${COCOA_FRAMEWORK}
    ${APPSERVICES_FRAMEWORK}
  )
  target_include_directories(${APP_NAME} PRIVATE ${STB_PATH})
  target_compile_definitions(${APP_NAME} PRIVATE GL_SILENCE_DEPRECATION)
else ()
  target_link_libraries(${APP_NAME}
    Framework
    ${OPENGL_LIBRARIES}
    ${WAYLAND_LIBRARIES}
  )
  target_include_directories(${APP_NAME} PRIVATE
    ${STB_PATH}
    ${WAYLAND_GEN_DIR}
    ${WAYLAND_INCLUDE_DIRS}
  )
endif ()

# Generate embedded shaders header at configure time.
set(EMBEDDED_SHADERS_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedShaders.hpp)
execute_process(
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.sh
    ${FRAMEWORK_SHADER_PATH}
    ${SAMPLE_SHADER_PATH}
    ${EMBEDDED_SHADERS_HEADER}
)
target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

