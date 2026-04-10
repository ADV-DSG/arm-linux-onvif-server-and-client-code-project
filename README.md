

工程代码实现了客户端和服务端，包括rtsp地址流发现、ptz云台控制：水平、垂直、调焦控制
目录结构介绍：
1、自己onvif工程
├── onvif-server-client-app        我自己创建的server和client代码，拷贝了gsoap生成的框架代码和自己写的部分
│   ├── CMakeLists-arm.txt
│   ├── CMakeLists.txt   自己实现
│   ├── CMakeLists-x86.txt
│   ├── common.h  自己实现
│   ├── dom.c
│   ├── dom.h
│   ├── main_client.c  自己实现
│   ├── main_server.c   自己实现
│   ├── make-arm.sh   自己实现
│   ├── mecevp.c
│   ├── mecevp.h
│   ├── onvif_server_interface.c  自己实现
│   ├── smdevp.c
│   ├── smdevp.h
│   ├── soapC.c
│   ├── soapClient.c
│   ├── soapH.h
│   ├── soapServer.c
│   ├── soapStub.h
│   ├── stdsoap2.c
│   ├── stdsoap2.h
│   ├── struct_timeval.c
│   ├── struct_timeval.h
│   ├── third-lib
│   │   └── openssl-1.1.1   交叉编译的openssl库
│   │       ├── bin
│   │       ├── include
│   │       ├── lib
│   │       ├── share
│   │       └── ssl
│   ├── threads.c
│   ├── threads.h
│   ├── wsaapi.c
│   ├── wsaapi.h
│   ├── wsdd.nsmap
│   ├── wsseapi.c
│   └── wsseapi.h




2、拷贝文件
从gsaop生成的onvif代码框架中把文件拷贝过来：
onvif-base-code/soap中就是生成的框架代码

cd  onvif-server-client-app/  我们自己的工程文件

cp ../onvif-base-code/soap/soapC.c .
cp ../onvif-base-code/onvif-code/soap/soapH.h .
cp ../onvif-base-code/soap/soapStub.h .
cp ../onvif-base-code/soap/soapClient.c .
cp ../onvif-base-code/soap/wsdd.nsmap .
cp ../onvif-base-code/gsoap/plugin/wsseapi.h .
cp ../onvif-base-code/gsoap/plugin/wsseapi.c .
cp ../onvif-base-code/gsoap/plugin/mecevp.h .
cp ../onvif-base-code/gsoap/plugin/mecevp.c .
cp ../onvif-base-code/gsoap/plugin/smdevp.h .
cp ../onvif-base-code/gsoap/plugin/smdevp.c .
cp ../onvif-base-code/gsoap/custom/struct_timeval.h .
cp ../onvif-base-code/gsoap/custom/struct_timeval.c .
cp ../onvif-base-code/gsoap/plugin/wsaapi.h .
cp ../onvif-base-code/gsoap/plugin/wsaapi.c .
cp ../onvif-base-code/gsoap/plugin/threads.h .
cp ../onvif-base-code/gsoap/plugin/threads.c .
cp ../onvif-base-code/gsoap/import/dom.h .
cp ../onvif-base-code/gsoap/dom.c .
cp ../onvif-base-code/gsoap/stdsoap2.c .
cp ../onvif-base-code/gsoap/stdsoap2.h .


3、自己实现文件：
onvif-server-client-app/ 以下需要自己实现
│   ├── CMakeLists-arm.txt
│   ├── CMakeLists.txt   自己实现：cmake编译文件
│   ├── CMakeLists-x86.txt
│   ├── common.h  自己实现 ：头文件定义
│   ├── main_client.c  自己实现：客户端代码，调用soapStub.h中的客户端接口
│   ├── main_server.c   自己实现：服务端代码，实现soapStub.h中的服务端接口
│   ├── make-arm.sh   自己实现：编译脚本
│   ├── onvif_server_interface.c  自己实现：服务端接口代码，实现soapStub.h中的服务端接口
│   ├── third-lib
│   │   └── openssl-1.1.1   移植的openssl库
│   │       ├── bin
│   │       ├── include
│   │       ├── lib
│   │       ├── share
│   │       └── ssl



4、openssl库的移植：
4.1 arm Linux移植openssl：
去官网下载代码：https://www.openssl.org/source/
1.执行下面的命令配置工程：注意arm-linux-gnueabihf 这多了 - ，因为每个代码自己风格不一样
 $ ./config no-asm shared no-async --prefix=$(pwd)/install --cross-compile-prefix=arm-linux-gnueabihf-   配置之后才会生成Makefile
其参数说明如下：
 no-asm: 在交叉编译过程中不使用汇编代码代码加速编译过程；
 shared: 生成动态连接库。
 no-async: 交叉编译工具链没有提供GNU C的ucontext库
 –prefix=: 安装路径
 –cross-compile-prefix=: 交叉编译工具
2.打开Makefile,删除里面所有的-m64和-m32编译选项。
编译openssl
1.执行make编译工程；
2.执行make install,在源文件路径会生成一个install文件

4.2 Windows中安装openssl库
下载库：win64位
http://www.winwin7.com/soft/32503.html#xiazai
安装到本地，安装到自己的目录：目录中出现bin  lib文件夹
bin/*dll
/lib/*.lib
引入VS：在VS中引入.lib  include，把dll放入exe目录

在VS中加入宏定义：
WITH_DOM    
WITH_OPENSSL
注意onvif代码是C语言，我们的VS工程是C++，必要的时候需要设置一下：
选中对应的C文件，右键-》属性-》C/C++ -》高级-》编译为：选择编译为C代码(/TC)




5、CMakeLists.txt编写
服务端程序  需要加上soapClient.c，因为
服务端需要用到：__wsdd__Probe =》soap_send___wsdd__ProbeMatches  这个函数是在soapClient.c实现
客户端：
set(CLIENT_SRC_LIST dom.c mecevp.c soapC.c stdsoap2.c threads.c wsseapi.c main_client.c smdevp.c soapClient.c struct_timeval.c wsaapi.c)
服务端：
set(SERVER_SRC_LIST dom.c mecevp.c soapC.c stdsoap2.c threads.c wsseapi.c main_server.c smdevp.c soapServer.c soapClient.c struct_timeval.c wsaapi.c onvif_server_interface.c)



6、make-arm.sh
mkdir build
cd build
cmake ../     生成Makefile
make       使用Makefile编译


生成：
build/ONVIF-SERVER
build/ONVIF-SERVER  服务端，放板卡运行
build/ONVIF-CLIENT 客户端
拷贝：
third-lib/openssl-1.1.1/lib/*.so到板卡/usr/lib  已经有了，就不用拷贝
build/ONVIF-SERVER 到板卡运行




7、运行log：
客户端第一步是使用UDP组播和服务端通信，获得了服务器IP地址后，就使用TCP和服务端通信，应用层是http协议

客户端：
[Interface 192.168.8.100] probeMatch->XAddrs:http://192.168.8.200:5000/onvif/device_service
deviceXAddr:http://192.168.8.200:5000/onvif/device_service
mediaXAddr:http://192.168.8.200:5000/onvif/media_service
ptzXAddr:http://192.168.8.200:5000/onvif/ptz_service
profilesToken:MediaProfile000
mCbDiscoveryNum:0, streamUri:rtsp://192.168.8.200:11554/live   获得rtsp流地址

服务端：
UDP IP = 192.168.1.100   UDP连接
udp socket connect 7
soap->tag = wsdd:Probe
g_device_xaddr:http://192.168.8.200:5000/onvif/device_service
g_uuid: urn:uuid:c6828889-41d0-4a7d-801e-2e7fe0f73f7c
__wsdd__Probe done

UDP IP = 192.168.8.100       TCP连接
udp socket connect 7
[DEBUG] TCP connection accepted from 192.168.8.100:6929
soap->tag = tds:GetCapabilities
[DEBUG] GetCapabilities called
[DEBUG] GetCapabilities: allocating Capabilities
[DEBUG] GetCapabilities: initializing Capabilities
[DEBUG] GetCapabilities: allocating Device capabilities
[DEBUG] GetCapabilities: allocating Device XAddr
[DEBUG] GetCapabilities: Device XAddr set to http://192.168.8.200:5000/onvif/device_service
[DEBUG] GetCapabilities: allocating Media capabilities
[DEBUG] GetCapabilities: allocating Media XAddr
[DEBUG] GetCapabilities: Media XAddr set to http://192.168.8.200:5000/onvif/media_service
[DEBUG] GetCapabilities: allocating StreamingCapabilities
[DEBUG] GetCapabilities: allocating PTZ capabilities
[DEBUG] GetCapabilities: allocating PTZ XAddr
[DEBUG] GetCapabilities: PTZ XAddr set to http://192.168.8.200:5000/onvif/ptz_service
[DEBUG] GetCapabilities: completed successfully
[DEBUG] Cleaning up soap environment
[DEBUG] Soap environment cleaned up
[DEBUG] Waiting for TCP connection...
[DEBUG] TCP connection accepted from 192.168.8.100:6930
[DEBUG] TCP connection accepted from 192.168.8.100:6931
soap->tag = trt:GetStreamUri      发送rtsp地址给客户端
GetStreamUri called




