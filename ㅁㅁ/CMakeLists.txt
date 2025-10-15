cmake_minimum_required(VERSION 3.16)
project(CozyVillage LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SFML 2.5 COMPONENTS system window graphics audio REQUIRED)

add_executable(CozyVillage
    src/main.cpp
)

target_link_libraries(CozyVillage
    sfml-system sfml-window sfml-graphics sfml-audio
)

# Windows 콘솔 창 숨김 옵션(원하면 주석 해제)
# if(WIN32)
#     set_target_properties(CozyVillage PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
# endif()
