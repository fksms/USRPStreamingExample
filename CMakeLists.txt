cmake_minimum_required(VERSION 3.13)
project(usrp_streaming_test)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules" ${CMAKE_MODULE_PATH})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-common -Wall -Wextra -Wno-unused-parameter -std=c99 -Werror=implicit-function-declaration")

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
find_package(FFTW3 REQUIRED)
pkg_check_modules(UHD REQUIRED uhd) # search library with pkg-config and name its results "UHD"

# 実行ファイルを作成
add_executable(${PROJECT_NAME} main.c usrp.c fft.c)

# インクルードするディレクトリの設定
target_include_directories(${PROJECT_NAME} PRIVATE
    ${UHD_INCLUDE_DIRS}
    ${FFTW3_INCLUDE_DIR}
)

# リンクするライブラリのディレクトリの設定
target_link_directories(${PROJECT_NAME} PRIVATE
    ${UHD_LIBRARY_DIRS}
    ${FFTW3_LIBRARY_DIRS}
)

# リンクするライブラリの設定
target_link_libraries(${PROJECT_NAME} PRIVATE
    Threads::Threads
    uhd
    fftw3
    fftw3_threads
)