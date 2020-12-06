cmake_minimum_required(VERSION 3.15)
project(src C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")

add_executable(simon simon.c game_logic.c main.c daqsim.c mongoose.c
        browsercontent.h mongoose.h daqsim.h)