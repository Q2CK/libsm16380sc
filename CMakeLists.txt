cmake_minimum_required(VERSION 3.22)

add_library(libsm16380sc
    src/sm16380sc.c
)

target_include_directories(libsm16380sc 
    PUBLIC
        inc
)