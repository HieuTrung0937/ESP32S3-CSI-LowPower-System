# esp32s3_csi_tx/CMakeLists.txt  và  esp32s3_csi_rx/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_csi_tx)         # hoặc _rx

# Thêm shared headers vào include path toàn project
include_directories(${CMAKE_SOURCE_DIR}/../shared)