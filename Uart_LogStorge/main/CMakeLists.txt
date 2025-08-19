file(GLOB_RECURSE SRCS_LIST "*.c")          # 递归查找所有.c文件

set(INCLUDE_FILES . uart tfcard ws2812)

idf_component_register(SRCS ${SRCS_LIST}
                       INCLUDE_DIRS ${INCLUDE_FILES}
                        )