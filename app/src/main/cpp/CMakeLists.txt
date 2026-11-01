cmake_minimum_required(VERSION 3.22.1)
project(usbexec C)

add_library(usbexec SHARED usbexec.c)
target_compile_features(usbexec PRIVATE c_std_11)
