# 1. Standard CMake Setup
cmake_minimum_required(VERSION 3.15)
project(AgoniaGame VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- FORCE STATIC RUNTIME (/MT or /MTd) ---
# This matches the Firebase SDK and fixes the massive flood of LNK2001 errors
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 2. Vcpkg Libraries (Replaced SFML with SDL2 ecosystem)
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)


# 3. Firebase C++ SDK Setup (Correct Path)
set(FIREBASE_CPP_SDK_DIR "C:/dev/firebase_cpp_sdk")
add_subdirectory(${FIREBASE_CPP_SDK_DIR} firebase_build EXCLUDE_FROM_ALL)

# 4. Add your source files
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

# 5. Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# ---> NEW: Force the .exe to build in the root directory <---
# We set it for Debug and Release specifically because Visual Studio generators 
# will otherwise try to append a "/Debug" folder to the path.
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
)

# 6. Link the libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
    # --- SDL2 Libraries ---
    SDL2::SDL2main
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_ttf::SDL2_ttf
    SDL2_mixer::SDL2_mixer
    # --- JSON ---
    nlohmann_json::nlohmann_json
    # --- Required Windows System Networking Libs for Firebase ---
    ws2_32
    crypt32
    secur32
    rpcrt4
    iphlpapi
    psapi
    userenv
    # --- Firebase Libraries ---
    firebase_database   
    firebase_app    
)