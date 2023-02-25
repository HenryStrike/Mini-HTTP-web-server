cmake_minimum_required(VERSION 3.2)
project(WebServer)

set(CMAKE_CXX_STANDARD 17)

include_directories(./include)
aux_source_directory(./src DIR_SRC)

add_executable(WebServer ${DIR_SRC})
