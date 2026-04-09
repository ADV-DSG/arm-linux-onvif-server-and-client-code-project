cmake_minimum_required(VERSION 3.5)
project(ONVIF-DEMO C CXX)

# ==========================================
# 交叉编译配置
# ==========================================
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 设置交叉编译器
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# 设置交叉编译工具链的根目录
set(TOOLCHAIN_PATH /usr/local/arm/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PATH})

# 查找策略
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# ==========================================
# 编译选项与头文件
# ==========================================
add_definitions(-DWITH_NONAMESPACES -DWITH_DOM -DWITH_OPENSSL)

# 包含目录
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/third-lib/openssl-1.1.1/include)

# 编译标志
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -g")
add_compile_options(-w)

# ==========================================
# 源文件定义
# ==========================================
set(CLIENT_SRC_LIST dom.c mecevp.c soapC.c stdsoap2.c threads.c wsseapi.c main_client.c smdevp.c soapClient.c struct_timeval.c wsaapi.c)
set(SERVER_SRC_LIST dom.c mecevp.c soapC.c stdsoap2.c threads.c wsseapi.c main_server.c smdevp.c soapServer.c soapClient.c struct_timeval.c wsaapi.c onvif_server_interface.c)

# ==========================================
# 动态库链接配置 (修改重点在这里)
# ==========================================

# 1. 指定动态库所在的目录
link_directories(${PROJECT_SOURCE_DIR}/third-lib/openssl-1.1.1/lib)

# 2. 客户端程序
add_executable(ONVIF-CLIENT ${CLIENT_SRC_LIST})
# 使用 -l 参数链接，CMake 会自动寻找 libssl.so 和 libcrypto.so
target_link_libraries(ONVIF-CLIENT ssl crypto pthread)

# 假设你已经把 third-lib 拷贝到了开发板
# cp third-lib/openssl-1.1.1/lib/libssl.so.1.1 /usr/lib/
# cp third-lib/openssl-1.1.1/lib/libcrypto.so.1.1 /usr/lib/
# ldconfig  # 刷新库缓存

# 3. 服务端程序
add_executable(ONVIF-SERVER ${SERVER_SRC_LIST})
target_link_libraries(ONVIF-SERVER ssl crypto pthread)