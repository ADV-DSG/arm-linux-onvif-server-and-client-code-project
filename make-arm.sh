mkdir build
cd build
cmake ../
make


# 生成：
# build/ONVIF-SERVER
# build/ONVIF-SERVER  服务端，放板卡运行
# build/ONVIF-CLIENT　客户端


# 拷贝：
# third-lib/openssl-1.1.1/lib/*.so到板卡/usr/lib  已经有了，就不用拷贝
# build/ONVIF-SERVER 到板卡运行


