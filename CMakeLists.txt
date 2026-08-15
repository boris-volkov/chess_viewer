cmake_minimum_required(VERSION 3.16)
project(chess_viewer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(chess_viewer chess_viewer.cpp)

find_package(SDL2 CONFIG QUIET)
find_package(SDL2_image CONFIG QUIET)

if (NOT TARGET SDL2::SDL2)
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(SDL2 REQUIRED sdl2)
        add_library(SDL2::SDL2 INTERFACE IMPORTED)
        target_include_directories(SDL2::SDL2 INTERFACE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(SDL2::SDL2 INTERFACE ${SDL2_LIBRARIES})
        target_compile_options(SDL2::SDL2 INTERFACE ${SDL2_CFLAGS_OTHER})
    endif()
endif()

if (NOT TARGET SDL2_image::SDL2_image)
    if (PkgConfig_FOUND)
        pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
        add_library(SDL2_image::SDL2_image INTERFACE IMPORTED)
        target_include_directories(SDL2_image::SDL2_image INTERFACE ${SDL2_IMAGE_INCLUDE_DIRS})
        target_link_libraries(SDL2_image::SDL2_image INTERFACE ${SDL2_IMAGE_LIBRARIES})
        target_compile_options(SDL2_image::SDL2_image INTERFACE ${SDL2_IMAGE_CFLAGS_OTHER})
    endif()
endif()

if (NOT TARGET SDL2::SDL2)
    message(FATAL_ERROR "SDL2 not found. Install SDL2 or use vcpkg.")
endif()
if (NOT TARGET SDL2_image::SDL2_image)
    message(FATAL_ERROR "SDL2_image not found. Install SDL2_image or use vcpkg.")
endif()

if (TARGET SDL2::SDL2main)
    target_link_libraries(chess_viewer PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(chess_viewer PRIVATE SDL2::SDL2 SDL2_image::SDL2_image)

# The app resolves games/pieces relative to its own executable directory
# (see SDL_GetBasePath() in main()), so keep local builds runnable the same
# way the packaged release zip is: stage the asset folders next to the binary.
add_custom_command(TARGET chess_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/games" "$<TARGET_FILE_DIR:chess_viewer>/games"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/pieces" "$<TARGET_FILE_DIR:chess_viewer>/pieces"
    COMMENT "Staging games/ and pieces/ next to chess_viewer"
)
