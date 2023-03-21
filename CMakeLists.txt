project(nkWebServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置 include Path
include_directories(./include)

# 设置源文件(需要编译的文件) 目录
aux_source_directory(./src DIR_SRCS)

add_executable(nkWebServer ${DIR_SRCS})

# target_link_libraries(webserver pthread mysqlclient)