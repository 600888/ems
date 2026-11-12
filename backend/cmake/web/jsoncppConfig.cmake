# Drogon 的包查找入口：直接复用父工程的源码目标，不导入另一套安装库。
if(NOT TARGET jsoncpp_static)
    set(jsoncpp_FOUND FALSE)
    set(jsoncpp_NOT_FOUND_MESSAGE "The bundled JsonCpp target has not been created")
    return()
endif()

set(jsoncpp_FOUND TRUE)
