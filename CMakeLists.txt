# 1. Standard CMake Setup
cmake_minimum_required(VERSION 3.15)
project(AgoniaGame VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- FORCE STATIC RUNTIME (/MT or /MTd) ---
# This ensures we don't need VCRUNTIME140.dll on other PCs.
# MultiThreaded = /MT (Release), MultiThreadedDebug = /MTd (Debug)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 2. Vcpkg Libraries
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# 3. Firebase C++ SDK Setup (Manual Pathing - MT Static Version)
set(FIREBASE_CPP_SDK_DIR "C:/dev/firebase_cpp_sdk")
set(FIREBASE_INCLUDE_DIR "${FIREBASE_CPP_SDK_DIR}/include")

# We point specifically to the MT (Static) x64 folder to match our /MT flag
set(FIREBASE_LIB_PATH "${FIREBASE_CPP_SDK_DIR}/libs/windows/vs2019/MT/x64")

# This tells CMake exactly which file to grab for each build configuration
set(FIREBASE_LIBS
    debug "${FIREBASE_LIB_PATH}/debug/firebase_app.lib"
    optimized "${FIREBASE_LIB_PATH}/release/firebase_app.lib"
    debug "${FIREBASE_LIB_PATH}/debug/firebase_database.lib"
    optimized "${FIREBASE_LIB_PATH}/release/firebase_database.lib"
)

# 4. Add source files
set(SOURCES 
    main.cpp
    Card.cpp
    Deck.cpp
    Table.cpp
    Player.cpp
    Match.cpp
    AIBot.cpp
    FirebaseFix.cpp
    LobbyManager.cpp
    GameWindow.cpp
)

# 5. Create executable
add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${FIREBASE_INCLUDE_DIR})

# Force the .exe to build in the root directory for easy testing
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
)

# --- NEW: IGNORE CONFLICTING LIBRARIES IN RELEASE ---
# This tells the linker: "If you see a library asking for Debug Runtime (MSVCRTD/LIBCMTD) 
# while I'm in Release mode, ignore that request."
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(${PROJECT_NAME} PROPERTIES 
        LINK_FLAGS "/NODEFAULTLIB:MSVCRTD /NODEFAULTLIB:LIBCMTD"
    )
endif()

# 6. Link the libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
    SDL2::SDL2main
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_ttf::SDL2_ttf
    SDL2_mixer::SDL2_mixer
    nlohmann_json::nlohmann_json
    ws2_32
    crypt32
    secur32
    rpcrt4
    iphlpapi
    psapi
    userenv
    ${FIREBASE_LIBS} # <--- Use our manual list here
)