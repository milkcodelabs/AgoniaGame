# 1. Standard CMake Setup
cmake_minimum_required(VERSION 3.15)
project(AgoniaGame VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- DESKTOP (VCPKG) DEPENDENCIES ---
if(NOT ANDROID)
    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_image CONFIG REQUIRED)
    find_package(SDL2_ttf CONFIG REQUIRED)
    find_package(SDL2_mixer CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

set(FIREBASE_CPP_SDK_DIR "C:/dev/firebase_cpp_sdk")
set(FIREBASE_INCLUDE_DIR "${FIREBASE_CPP_SDK_DIR}/include")

# --- WINDOWS SPECIFIC SETUP ---
if(WIN32)
    # FORCE STATIC RUNTIME (/MT or /MTd)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    # Firebase C++ SDK Setup (Manual Pathing - MT Static Version)
    set(FIREBASE_LIB_PATH "${FIREBASE_CPP_SDK_DIR}/libs/windows/vs2019/MT/x64")

    set(FIREBASE_LIBS
        debug "${FIREBASE_LIB_PATH}/debug/firebase_app.lib"
        optimized "${FIREBASE_LIB_PATH}/release/firebase_app.lib"
        debug "${FIREBASE_LIB_PATH}/debug/firebase_database.lib"
        optimized "${FIREBASE_LIB_PATH}/release/firebase_database.lib"
    )
endif()

# --- ANDROID SPECIFIC SETUP (SOURCE ROUTE) ---
if(ANDROID)
    # Add the SDL source subdirectories to be compiled natively
    add_subdirectory(third_party/SDL2)
    add_subdirectory(third_party/SDL2_image)
    add_subdirectory(third_party/SDL2_ttf)
    add_subdirectory(third_party/SDL2_mixer)
endif()

# 4. Add source files
set(SOURCES 
    src/main.cpp
    src/Card.cpp
    src/Deck.cpp
    src/Table.cpp
    src/Player.cpp
    src/Match.cpp
    src/AIBot.cpp
    src/FirebaseFix.cpp
    src/LobbyManager.cpp
    src/GameWindow.cpp
)

# Add the Windows Executable Icon
if(WIN32 AND NOT ANDROID)
    list(APPEND SOURCES src/win32_icon.rc)
endif()

# Add the SDL Android Entry Point
if(ANDROID)
    list(APPEND SOURCES third_party/SDL2/src/main/android/SDL_android_main.c)
endif()

# 5. Create executable (Desktop) or shared library (Android)
if(ANDROID)
    add_library(${PROJECT_NAME} SHARED ${SOURCES})
    
    # NEW: Force the Android compiler to output "libmain.so" instead of "libAgoniaGame.so"
    # This prevents us from having to modify SDLActivity.java's hardcoded launch sequence.
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "main")
else()
    add_executable(${PROJECT_NAME} ${SOURCES})
endif()

# --- INCLUDE DIRECTORIES ---
target_include_directories(${PROJECT_NAME} PRIVATE ${FIREBASE_INCLUDE_DIR})

if(ANDROID)
    # Ensure your engine can find the SDL headers inside the third_party folders
    target_include_directories(${PROJECT_NAME} PRIVATE 
        third_party/SDL2/include
        third_party/SDL2_image
        third_party/SDL2_ttf
        third_party/SDL2_mixer
    )
endif()

# Force the output to build in the root directory for easy testing (Desktop)
if(NOT ANDROID)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
    )
endif()

# IGNORE CONFLICTING LIBRARIES IN RELEASE (Windows MSVC only)
if(MSVC AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(${PROJECT_NAME} PROPERTIES 
        LINK_FLAGS "/NODEFAULTLIB:MSVCRTD /NODEFAULTLIB:LIBCMTD"
    )
endif()

# --- LINK LIBRARIES ---
if(ANDROID)
    # Target the specific Firebase libraries for the device currently being built
    # Note: Depending on your exact SDK version, the path might be just ${ANDROID_ABI} without the /c++ at the end. 
    # If the build fails finding the library, remove the /c++
    set(FB_APP_LIB "${FIREBASE_CPP_SDK_DIR}/libs/android/${ANDROID_ABI}/libfirebase_app.a")
    set(FB_DB_LIB "${FIREBASE_CPP_SDK_DIR}/libs/android/${ANDROID_ABI}/libfirebase_database.a")

    # Link the native Android targets
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        SDL2 
        SDL2_image 
        SDL2_ttf 
        SDL2_mixer
        log      
        android  
        ${FB_APP_LIB}
        ${FB_DB_LIB}
    )
else()
    # Link Vcpkg Windows targets
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        SDL2::SDL2main
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_ttf::SDL2_ttf
        SDL2_mixer::SDL2_mixer
        nlohmann_json::nlohmann_json
    )
endif()

# Append Windows-specific libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32 crypt32 secur32 rpcrt4 iphlpapi psapi userenv
        ${FIREBASE_LIBS}
    )
endif()