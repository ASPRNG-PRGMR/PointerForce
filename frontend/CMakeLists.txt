cmake_minimum_required(VERSION 3.10)
project(pointerforce-webui CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon REQUIRED)

add_executable(pointerforce-webui
    main.cpp
    services/ControlClient.cc
    services/ConfigStore.cc
)

target_link_libraries(pointerforce-webui PRIVATE Drogon::Drogon pthread)
