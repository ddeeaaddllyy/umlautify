cmake_minimum_required(VERSION 3.20)
project(umlautify CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(umlautify main.cpp)
target_link_libraries(umlautify PRIVATE user32)

if(MSVC)
    target_compile_options(umlautify PRIVATE /O2 /Oi /Ot /GL)
    target_link_options(umlautify PRIVATE /LTCG)
else()
    target_compile_options(umlautify PRIVATE -O3 -march=native -flto)
    target_link_options(umlautify PRIVATE -flto)
endif()
