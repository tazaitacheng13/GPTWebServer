cmake_minimum_required(VERSION 3.0.0)
project(webserver VERSION 0.1.0 LANGUAGES C CXX)

# file(GLOB SOURCES "*.cpp")

# # 排除文件
# list (REMOVE_ITEM SOURCES
#     "main.cpp"
# )
# set (SOURCES
#     ./http/http_conn.cpp
#     main_nonactive.cpp
#     ./db/db.cpp
#     ./log/log.cpp
#     )

set (SOURCES
    ./http/http_conn.cpp
    main.cpp
    ./db/db.cpp
    ./log/log.cpp
    ./timer/lst_timer.cpp
    webserver.cpp
    parse_arg.cpp
    )

add_executable(webserver ${SOURCES})


set(PYTHON_EXECUTABLE "/root/anaconda3/envs/AI/bin/python3.9")
set(PYTHON_INCLUDE_DIR "/root/anaconda3/envs/AI/include/python3.9")
set(PYTHON_LIBRARY "/root/anaconda3/envs/AI/lib/libpython3.9.so")

# 找到Python解释器、库和头文件
find_package(PythonInterp 3.9 REQUIRED)
find_package(PythonLibs 3.9 REQUIRED)
find_package(CURL REQUIRED)

# 包含Python头文件目录
include_directories(${PYTHON_INCLUDE_DIRS} ${PYTHON_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS} )

# 链接 pthread 库
target_link_libraries(webserver pthread mysqlclient ffi ${CURL_LIBRARIES} ${PYTHON_LIBRARIES} ${PYTHON_LIBRARY})