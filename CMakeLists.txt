cmake_minimum_required(VERSION 3.13)
project(usrp_streaming_test)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules" ${CMAKE_MODULE_PATH})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-common -Wall -Wextra -Wno-unused-parameter -std=c99 -Werror=implicit-function-declaration -Wno-deprecated-declarations")

set(CMAKE_C_FLAGS_DEBUG "-ggdb3 -g3 -O0 -fsanitize=address")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -fsanitize=address")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -ggdb3 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O3")

message("## CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}") # ビルドタイプ確認

# pthreadを使う
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# パッケージの確認
find_package(PkgConfig REQUIRED)
find_package(UHD REQUIRED)
find_package(FFTW REQUIRED)
find_package(Liquid REQUIRED)

# 実行ファイルを作成
#add_executable(${PROJECT_NAME} main.c usrp.c udp_send.c)
#add_executable(${PROJECT_NAME} main.c usrp.c fft.c take_queue.c)
add_executable(${PROJECT_NAME} main.c usrp.c pfbch.c take_queue.c)

# インクルードするディレクトリの設定
target_include_directories(${PROJECT_NAME} PRIVATE
    ${UHD_INCLUDE_DIRS}
    ${FFTW_INCLUDE_DIRS}
    ${LIQUID_INCLUDE_DIRS}
)

# リンクするライブラリの設定
target_link_libraries(${PROJECT_NAME} PRIVATE
    Threads::Threads
    ${UHD_LIBRARIES}
    #${FFTW_LIBRARIES}
    ${FFTW_DOUBLE_LIB} # FFTWのライブラリはdoubleのみ使用
    ${LIQUID_LIBRARIES}
)