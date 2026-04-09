#include "soapH.h"
#include "soapStub.h"
#include "wsaapi.h"
#include "wsdd.nsmap"
#include "wsseapi.h"
#include <assert.h>

// 平台特定头文件
#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// SIO_UDP_CONNRESET定义（如果未定义）
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#else
#include <unistd.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#define USERNAME "admin"
#define PASSWORD "admin123"

#define SOAP_ASSERT assert
#define SOAP_DBGLOG printf
#define SOAP_DBGERR printf

#define SOAP_TO "urn:schemas-xmlsoap-org:ws:2005:04:discovery"
#define SOAP_ACTION "http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe"

#define SOAP_MCAST_ADDR "soap.udp://239.255.255.250:3702" // onvif规定的组播地址

#define SOAP_ITEM ""                            // 寻找的设备范围
#define SOAP_TYPES "dn:NetworkVideoTransmitter" // 寻找的设备类型

#define SOAP_SOCK_TIMEOUT (1) // socket超时时间（单秒秒）
#define nullptr NULL
#define bool int
#define true 1
#define false 0

// 平台特定的延时函数
#ifdef _WIN32
#define sleep_ms(ms) Sleep(ms)
#else
#define sleep_ms(ms) usleep(ms*1000)
#endif

void soap_perror(struct soap* soap, const char* str)
{
    if (nullptr == str) {
        SOAP_DBGERR("[soap] error: %d, %s, %s\n", soap->error, *soap_faultcode(soap), *soap_faultstring(soap));
    }
    else {
        SOAP_DBGERR("[soap] %s error: %d, %s, %s\n", str, soap->error, *soap_faultcode(soap), *soap_faultstring(soap));
    }
}

void* ONVIF_soap_malloc(struct soap* soap, unsigned int n)
{
    void* p = nullptr;

    if (n > 0) {
        p = soap_malloc(soap, n);
        if (nullptr != p) {
            memset(p, 0x00, n);
        }
        else {
            printf("malloc p failed \n");
        }
    }
    return p;
}

struct soap* ONVIF_soap_new(int timeout)
{
    struct soap* soap = nullptr; // soap环境变量

    // 不使用SOAP_ASSERT，而是使用条件检查
    soap = soap_new();
    if (nullptr == soap) {
        printf("[ERROR] ONVIF_soap_new: failed to create soap context\n");
        return nullptr;
    }

    soap_set_namespaces(soap, namespaces); // 设置soap的namespaces
    soap->recv_timeout = timeout;          // 设置超时（超过指定时间没有数据就退出）
    soap->send_timeout = timeout;
    soap->connect_timeout = timeout;

#if defined(__linux__) || defined(__linux) // 参考https://www.genivia.com/dev.html#client-c的修改：
    soap->socket_flags = MSG_NOSIGNAL;     // To prevent connection reset errors
#elif defined(_WIN32)
    // Windows平台特定设置
    // 禁用UDP连接重置错误
    DWORD dwBytesReturned = 0;
    BOOL bNewBehavior = FALSE;
    // 只有在soap->master不为null时才调用WSAIoctl
    if (soap->master) {
        WSAIoctl(soap->master, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior),
            NULL, 0, &dwBytesReturned, NULL, NULL);
    }
#endif

    soap_set_mode(soap, SOAP_C_UTFSTRING); // 设置为UTF-8编码，否则叠加中文OSD会乱码

    return soap;
}

void ONVIF_soap_delete(struct soap* soap)
{
    soap_destroy(soap); // remove deserialized class instances (C++ only)
    soap_end(soap);     // Clean up deserialized data (except class instances) and temporary data
    soap_done(soap);    // Reset, close communications, and remove callbacks
    soap_free(soap);    // Reset and deallocate the context created with soap_new or soap_copy
}

#ifdef _WIN32
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

// 获取所有网络接口的IP地址
int get_all_local_ips(char* ips[], int max_count) {
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
    DWORD dwRetVal = 0;
    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);
    int count = 0;

    pAdapterInfo = (IP_ADAPTER_INFO*)malloc(sizeof(IP_ADAPTER_INFO));
    if (pAdapterInfo == NULL) {
        printf("Error allocating memory needed to call GetAdaptersInfo\n");
        return 0;
    }

    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        free(pAdapterInfo);
        pAdapterInfo = (IP_ADAPTER_INFO*)malloc(ulOutBufLen);
        if (pAdapterInfo == NULL) {
            printf("Error allocating memory needed to call GetAdaptersInfo\n");
            return 0;
        }
    }

    if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) == NO_ERROR) {
        pAdapter = pAdapterInfo;
        while (pAdapter && count < max_count) {
            PIP_ADDR_STRING pAddrList = &(pAdapter->IpAddressList);
            while (pAddrList && count < max_count) {
                // 跳过127.0.0.1和0.0.0.0
                if (strcmp(pAddrList->IpAddress.String, "127.0.0.1") != 0 &&
                    strcmp(pAddrList->IpAddress.String, "0.0.0.0") != 0) {
                    ips[count] = strdup(pAddrList->IpAddress.String);
                    if (ips[count]) {
                        count++;
                    }
                }
                pAddrList = pAddrList->Next;
            }
            pAdapter = pAdapter->Next;
        }
    } else {
        printf("GetAdaptersInfo failed with error: %d\n", dwRetVal);
    }

    free(pAdapterInfo);
    return count;
}

#else
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>

// 获取所有网络接口的IP地址
int get_all_local_ips(char* ips[], int max_count) {
    struct ifaddrs *ifaddr, *ifa;
    int count = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return 0;
    }

    for (ifa = ifaddr; ifa != NULL && count < max_count; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char *ip = inet_ntoa(addr->sin_addr);
            
            // 跳过127.0.0.1和0.0.0.0
            if (strcmp(ip, "127.0.0.1") != 0 && strcmp(ip, "0.0.0.0") != 0) {
                ips[count] = strdup(ip);
                if (ips[count]) {
                    count++;
                }
            }
        }
    }

    freeifaddrs(ifaddr);
    return count;
}
#endif

/************************************************************************
**函数：ONVIF_init_header
**功能：初始化soap描述消息头
**参数：
        [in] soap - soap环境变量
**返回：无
**备注：
    1). 在本函数内部通过ONVIF_soap_malloc分配的内存，将在ONVIF_soap_delete中被释放
************************************************************************/
void ONVIF_init_header(struct soap* soap)
{
    struct SOAP_ENV__Header* header = nullptr;

    if (nullptr == soap) {
        printf("[ERROR] ONVIF_init_header: soap is null\n");
        return;
    }

    header = (struct SOAP_ENV__Header*)ONVIF_soap_malloc(soap, sizeof(struct SOAP_ENV__Header));
    if (nullptr == header) {
        printf("[ERROR] ONVIF_init_header: failed to allocate header\n");
        return;
    }

    soap_default_SOAP_ENV__Header(soap, header);

    // 生成UUID
    header->wsa__MessageID = (char*)soap_wsa_rand_uuid(soap);
    if (nullptr == header->wsa__MessageID) {
        printf("[ERROR] ONVIF_init_header: failed to generate MessageID\n");
        return;
    }

    // 分配并设置To字段
    header->wsa__To = (char*)ONVIF_soap_malloc(soap, strlen(SOAP_TO) + 1);
    if (nullptr == header->wsa__To) {
        printf("[ERROR] ONVIF_init_header: failed to allocate wsa__To\n");
        return;
    }
    strcpy(header->wsa__To, SOAP_TO);

    // 分配并设置Action字段
    header->wsa__Action = (char*)ONVIF_soap_malloc(soap, strlen(SOAP_ACTION) + 1);
    if (nullptr == header->wsa__Action) {
        printf("[ERROR] ONVIF_init_header: failed to allocate wsa__Action\n");
        return;
    }
    strcpy(header->wsa__Action, SOAP_ACTION);

    soap->header = header;
}

/************************************************************************
**函数：ONVIF_init_ProbeType
**功能：初始化探测设备的范围和类型
**参数：
        [in]  soap  - soap环境变量
        [out] probe - 填充要探测的设备范围和类型
**返回：
        0表明探测到，非0表明未探测到
**备注：
    1). 在本函数内部通过ONVIF_soap_malloc分配的内存，将在ONVIF_soap_delete中被释放
************************************************************************/
void ONVIF_init_ProbeType(struct soap* soap, struct wsdd__ProbeType* probe)
{
    struct wsdd__ScopesType* scope = nullptr; // 用于描述查找哪类的Web服务

    if (nullptr == soap) {
        printf("ONVIF_init_ProbeType: soap is null \n");
        return;
    }
    if (nullptr == probe) {
        printf("ONVIF_init_ProbeType: probe is null \n");
        return;
    }
    scope = (struct wsdd__ScopesType*)ONVIF_soap_malloc(soap, sizeof(struct wsdd__ScopesType));
    soap_default_wsdd__ScopesType(soap, scope); // 设置寻找设备的范围
    scope->__item = (char*)ONVIF_soap_malloc(soap, strlen(SOAP_ITEM) + 1);
    strcpy(scope->__item, SOAP_ITEM);

    memset(probe, 0x00, sizeof(struct wsdd__ProbeType));
    soap_default_wsdd__ProbeType(soap, probe);
    probe->Scopes = scope;
    probe->Types = (char*)ONVIF_soap_malloc(soap, strlen(SOAP_TYPES) + 1); // 设置寻找设备的类型
    strcpy(probe->Types, SOAP_TYPES);
}

void ONVIF_DetectDevice(void (*cb)(char* DeviceXAddr))
{
    int i;
    int result = 0;
    unsigned int count = 0;          // 搜索到的设备个数
    struct soap* soap = nullptr;     // soap环境变量
    struct wsdd__ProbeType req;      // 用于发送Probe消息
    struct __wsdd__ProbeMatches rep; // 用于接收Probe应答
    struct wsdd__ProbeMatchType* probeMatch;
    
    // 获取所有网络接口的IP地址
    char* local_ips[16];
    int ip_count = get_all_local_ips(local_ips, 16);
    
    printf("[DEBUG] Found %d network interfaces\n", ip_count);
    for (i = 0; i < ip_count; i++) {
        printf("[DEBUG] Network interface %d: %s\n", i, local_ips[i]);
    }

    // 如果没有找到网络接口，使用默认方式
    if (ip_count == 0) {
        printf("[WARNING] No network interfaces found, using default method\n");
        soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
        if (nullptr == soap) {
            printf("[ERROR] ONVIF_DetectDevice: failed to create soap context\n");
            return;
        }

        ONVIF_init_header(soap);
        ONVIF_init_ProbeType(soap, &req);
        result = soap_send___wsdd__Probe(soap, SOAP_MCAST_ADDR, nullptr, &req);
        while (SOAP_OK == result) {
            memset(&rep, 0x00, sizeof(rep));
            result = soap_recv___wsdd__ProbeMatches(soap, &rep);
            if (SOAP_OK == result) {
                if (soap->error) {
                    soap_perror(soap, "ProbeMatches");
                }
                else {
                    if (nullptr != rep.wsdd__ProbeMatches) {
                        count += rep.wsdd__ProbeMatches->__sizeProbeMatch;
                        for (i = 0; i < rep.wsdd__ProbeMatches->__sizeProbeMatch; i++) {
                            probeMatch = rep.wsdd__ProbeMatches->ProbeMatch + i;
                            if (nullptr != cb) {
                                if (probeMatch->XAddrs) {
                                    printf("probeMatch->XAddrs:%s \n", probeMatch->XAddrs);
                                    cb(probeMatch->XAddrs);
                                }
                            }
                        }
                    }
                }
            }
            else if (soap->error) {
                break;
            }
        }

        SOAP_DBGLOG("\ndetect end! It has detected %d devices!\n", count);

        if (nullptr != soap) {
            ONVIF_soap_delete(soap);
        }
        return;
    }

    // 遍历所有网络接口，为每个接口发送UDP探测
    for (int iface = 0; iface < ip_count; iface++) {
        printf("[DEBUG] Sending probe from interface %s\n", local_ips[iface]);
        
        soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
        if (nullptr == soap) {
            printf("[ERROR] ONVIF_DetectDevice: failed to create soap context for interface %s\n", local_ips[iface]);
            continue;
        }

        // 设置为UDP模式
        soap->omode = SOAP_IO_UDP;
        
        // 绑定到特定的本地接口
        if (!soap_valid_socket(soap_bind(soap, local_ips[iface], 0, 1))) {
            printf("[ERROR] Failed to bind to interface %s: %d\n", local_ips[iface], soap->error);
            ONVIF_soap_delete(soap);
            continue;
        }

        ONVIF_init_header(soap);
        ONVIF_init_ProbeType(soap, &req);
        
        result = soap_send___wsdd__Probe(soap, SOAP_MCAST_ADDR, nullptr, &req);
        if (result != SOAP_OK) {
            printf("[ERROR] Failed to send probe from interface %s: %d\n", local_ips[iface], result);
            ONVIF_soap_delete(soap);
            continue;
        }

        // 接收设备应答
        while (SOAP_OK == result) {
            memset(&rep, 0x00, sizeof(rep));
            result = soap_recv___wsdd__ProbeMatches(soap, &rep);
            if (SOAP_OK == result) {
                if (soap->error) {
                    soap_perror(soap, "ProbeMatches");
                }
                else {
                    if (nullptr != rep.wsdd__ProbeMatches) {
                        count += rep.wsdd__ProbeMatches->__sizeProbeMatch;
                        for (i = 0; i < rep.wsdd__ProbeMatches->__sizeProbeMatch; i++) {
                            probeMatch = rep.wsdd__ProbeMatches->ProbeMatch + i;
                            if (nullptr != cb) {
                                if (probeMatch->XAddrs) {
                                    printf("[Interface %s] probeMatch->XAddrs:%s \n", local_ips[iface], probeMatch->XAddrs);
                                    cb(probeMatch->XAddrs);
                                }
                            }
                        }
                    }
                }
            }
            else if (soap->error) {
                break;
            }
        }

        ONVIF_soap_delete(soap);
    }

    // 释放IP地址字符串
    for (i = 0; i < ip_count; i++) {
        if (local_ips[i]) {
            free(local_ips[i]);
        }
    }

    SOAP_DBGLOG("\ndetect end! It has detected %d devices!\n", count);
}

#define SOAP_CHECK_ERROR(result, soap, str)                    \
    do {                                                       \
        if (SOAP_OK != (result) || SOAP_OK != (soap)->error) { \
            soap_perror((soap), (str));                        \
            if (SOAP_OK == (result)) {                         \
                (result) = (soap)->error;                      \
            }                                                  \
            goto EXIT;                                         \
        }                                                      \
    } while (0)

/************************************************************************
**函数：ONVIF_SetAuthInfo
**功能：设置认证信息
**参数：
        [in] soap     - soap环境变量
        [in] username - 用户名
        [in] password - 密码
**返回：
        0表明成功，非0表明失败
**备注：
************************************************************************/
static int ONVIF_SetAuthInfo(struct soap* soap, const char* username, const char* password)
{
    int result = 0;

    // 使用条件检查替代SOAP_ASSERT
    if (nullptr == soap) {
        printf("[ERROR] ONVIF_SetAuthInfo: soap is null\n");
        return -1;
    }

    if (nullptr == username) {
        printf("[ERROR] ONVIF_SetAuthInfo: username is null\n");
        return -1;
    }

    if (nullptr == password) {
        printf("[ERROR] ONVIF_SetAuthInfo: password is null\n");
        return -1;
    }

    // 调用soap_wsse_add_UsernameTokenDigest函数
    result = soap_wsse_add_UsernameTokenDigest(soap, NULL, username, password);
    if (result != SOAP_OK) {
        printf("[ERROR] ONVIF_SetAuthInfo: soap_wsse_add_UsernameTokenDigest failed, result: %d\n", result);
        if (soap->error != SOAP_OK) {
            printf("[ERROR] ONVIF_SetAuthInfo: soap error: %d\n", soap->error);
        }
        return result;
    }

    return result;
}


/************************************************************************
**函数：ONVIF_GetDeviceInformation
**功能：获取设备基本信息
**参数：
        [in] DeviceXAddr - 设备服务地址
**返回：
        0表明成功，非0表明失败
**备注：
************************************************************************/
int ONVIF_GetDeviceInformation(const char* DeviceXAddr)
{
    int result = 0;
    struct soap* soap = nullptr;
    struct _tds__GetDeviceInformation devinfo_req;
    struct _tds__GetDeviceInformationResponse devinfo_resp;


    if (nullptr == DeviceXAddr) {
        printf("[ERROR] ONVIF_GetDeviceInformation: DeviceXAddr is null\n");
        return -1;
    }
    soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
    if (nullptr == soap) {
        printf("[ERROR] ONVIF_GetDeviceInformation: soap is null\n");
        return -1;
    }

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

    result = soap_call___tds__GetDeviceInformation(soap, DeviceXAddr, nullptr, &devinfo_req, &devinfo_resp);
    SOAP_CHECK_ERROR(result, soap, "GetDeviceInformation");
    // std::cout << "      Manufacturer:\t" << devinfo_resp.Manufacturer << "\n";
    // std::cout << "      Model:\t" << devinfo_resp.Model << "\n";
    // std::cout << "      FirmwareVersion:\t" << devinfo_resp.FirmwareVersion << "\n";
    // std::cout << "      SerialNumber:\t" << devinfo_resp.SerialNumber << "\n";
    // std::cout << "      HardwareId:\t" << devinfo_resp.HardwareId << "\n";

EXIT:

    if (nullptr != soap) {
        ONVIF_soap_delete(soap);
    }
    return result;
}

/************************************************************************
**函数：ONVIF_GetSnapshotUri
**功能：获取设备图像抓拍地址(HTTP)
**参数：
        [in]  MediaXAddr    - 媒体服务地址
        [in]  ProfileToken  - the media profile token
        [out] uri           - 返回的地址
        [in]  sizeuri       - 地址缓存大小
**返回：
        0表明成功，非0表明失败
**备注：
    1). 并非所有的ProfileToken都支持图像抓拍地址。举例：XXX品牌的IPC有如下三个配置profile0/profile1/TestMediaProfile，其中TestMediaProfile返回的图像抓拍地址就是空指针。
************************************************************************/
int ONVIF_GetSnapshotUri(const char* MediaXAddr, const char* ProfileToken, char** snapUri)
{
    int result = 0;
    struct soap* soap = nullptr;
    struct _trt__GetSnapshotUri req;
    struct _trt__GetSnapshotUriResponse rep;


    if (nullptr == MediaXAddr) {
        printf("[ERROR] ONVIF_GetSnapshotUri: MediaXAddr is null\n");
        return -1;
    }
    if (nullptr == ProfileToken) {
        printf("[ERROR] ONVIF_GetSnapshotUri: ProfileToken is null\n");
        return -1;
    }
    soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
    if (nullptr == soap) {
        printf("[ERROR] ONVIF_GetSnapshotUri: soap is null\n");
        return -1;
    }
    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);
    req.ProfileToken = (char*)ProfileToken;
    result = soap_call___trt__GetSnapshotUri(soap, MediaXAddr, NULL, &req, &rep);
    SOAP_CHECK_ERROR(result, soap, "GetSnapshotUri");

    if (nullptr != rep.MediaUri && nullptr != rep.MediaUri->Uri) {
        *snapUri = rep.MediaUri->Uri;
    }

EXIT:

    if (NULL != soap) {
        ONVIF_soap_delete(soap);
    }

    return result;
}

bool ONVIF_GetProfiles(const char* mediaXAddr, char* profilesToken)
{
    int result = 0;
    struct soap* soap = nullptr;
    struct _trt__GetProfiles devinfo_req;
    struct _trt__GetProfilesResponse devinfo_resp;

    if (nullptr == mediaXAddr) {
        printf("[ERROR] ONVIF_GetProfiles: mediaXAddr is null\n");
        return false;
    }
    soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
    if (nullptr == soap) {
        printf("[ERROR] ONVIF_GetProfiles: failed to create soap context\n");
        return false;
    }
    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);
    result = soap_call___trt__GetProfiles(soap, mediaXAddr, nullptr, &devinfo_req, &devinfo_resp);
    SOAP_CHECK_ERROR(result, soap, "ONVIF_GetProfiles");

    //SOAP_ASSERT(devinfo_resp.__sizeProfiles > 0);
    if (devinfo_resp.__sizeProfiles <= 0) {
        printf("[ERROR] ONVIF_GetProfiles: devinfo_resp.__sizeProfiles <= 0\n");
        return false;
    }
    // *profilesToken = devinfo_resp.Profiles->token;

    if (devinfo_resp.Profiles != nullptr && devinfo_resp.__sizeProfiles > 0) {
        snprintf(profilesToken, 1024, "%s", devinfo_resp.Profiles->token);
        printf("ONVIF_GetProfiles:profilesToken:%s \n", profilesToken);
    }
    else {
        printf("ONVIF_GetProfiles: No profiles found\n");
        result = -1;
    }


EXIT:
    if (nullptr != soap) {
        ONVIF_soap_delete(soap);
    }

    return result;
}
/************************************************************************
**函数：ONVIF_GetCapabilities
**功能：获取设备能力信息
**参数：
        [in] DeviceXAddr - 设备服务地址
        [out] mediaXAddr - 媒体服务地址
        [out] ptzXAddr - PTZ服务地址
**返回：
        0表明成功，非0表明失败
**备注：
    1). 其中最主要的参数之一是媒体服务地址
************************************************************************/
int ONVIF_GetCapabilities(const char* deviceXAddr, char* mediaXAddr, char* ptzXAddr)
{
    int result = 0;
    struct soap* soap = nullptr;
    struct _tds__GetCapabilities devinfo_req;
    struct _tds__GetCapabilitiesResponse devinfo_resp;

    printf("ONVIF_GetCapabilities:mediaXAddr---1:%s \n", mediaXAddr);
    // 使用条件检查替代SOAP_ASSERT
    if (deviceXAddr == NULL) {
        printf("[ERROR] ONVIF_GetCapabilities: deviceXAddr is null\n");
        return -1;
    }

    soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
    if (nullptr == soap) {
        printf("[ERROR] ONVIF_GetCapabilities: failed to create soap context\n");
        return -1;
    }

    // 调用ONVIF_SetAuthInfo函数
    result = ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);
    if (result != 0) {
        printf("[ERROR] ONVIF_GetCapabilities: failed to set auth info\n");
        goto EXIT;
    }
    soap_default__tds__GetCapabilities(soap, &devinfo_req);
    soap_default__tds__GetCapabilitiesResponse(soap, &devinfo_resp);
    result = soap_call___tds__GetCapabilities(soap, deviceXAddr, NULL, &devinfo_req, &devinfo_resp);
    SOAP_CHECK_ERROR(result, soap, "GetCapabilities");
    printf("[DEBUG] GetCapabilities response received, Capabilities: %p\n", devinfo_resp.Capabilities);
    if (devinfo_resp.Capabilities) {
        printf("[DEBUG] GetCapabilities Media: %p\n", devinfo_resp.Capabilities->Media);
        if (devinfo_resp.Capabilities->Media) {
            printf("[DEBUG] GetCapabilities Media XAddr: %s\n", devinfo_resp.Capabilities->Media->XAddr);
        }
        if (devinfo_resp.Capabilities->PTZ) {
            printf("[DEBUG] GetCapabilities PTZ: %p\n", devinfo_resp.Capabilities->PTZ);
            printf("[DEBUG] GetCapabilities PTZ XAddr: %s\n", devinfo_resp.Capabilities->PTZ->XAddr);
        }
    }

    printf("ONVIF_GetCapabilities:mediaXAddr---:%s \n", mediaXAddr);

    if (devinfo_resp.Capabilities && devinfo_resp.Capabilities->Media != nullptr) {
        snprintf(mediaXAddr, 1024, "%s", devinfo_resp.Capabilities->Media->XAddr);
        printf("ONVIF_GetCapabilities:mediaXAddr:%s \n", mediaXAddr);
    }
    else {
        printf("[ERROR] GetCapabilities: Capabilities or Media is NULL\n");
        result = -1;
    }

    if (devinfo_resp.Capabilities && devinfo_resp.Capabilities->PTZ != nullptr) {
        snprintf(ptzXAddr, 1024, "%s", devinfo_resp.Capabilities->PTZ->XAddr);
        printf("ONVIF_GetCapabilities:ptzXAddr:%s \n", ptzXAddr);
    }
    else {
        printf("[ERROR] GetCapabilities: Capabilities or PTZ is NULL\n");
        // 不设置result为错误，因为设备可能不支持PTZ
    }

EXIT:

    if (nullptr != soap) {
        ONVIF_soap_delete(soap);
    }
    return result;
}

/************************************************************************
**函数：make_uri_withauth
**功能：构造带有认证信息的URI地址
**参数：
        [in]  src_uri       - 未带认证信息的URI地址
        [in]  username      - 用户名
        [in]  password      - 密码
        [out] dest_uri      - 返回的带认证信息的URI地址
        [in]  size_dest_uri - dest_uri缓存大小
**返回：
        0成功，非0失败
**备注：
    1). 例子：
    无认证信息的uri：rtsp://100.100.100.140:554/av0_0
    带认证信息的uri：rtsp://username:password@100.100.100.140:554/av0_0
************************************************************************/
static int make_uri_withauth(const char* src_uri, const char* username, char* password, char** dest_uri)
{
    int result = 0;

    if (src_uri == NULL) {
        printf("[ERROR] make_uri_withauth: src_uri is null\n");
        return -1;
    }

    if (username == NULL && password == NULL) { // 生成新的uri地址
        *dest_uri = src_uri;
    }
    else {
        /*
        std::string::size_type position = src_uri.find("//");
        if (std::string::npos == position) {
            SOAP_DBGERR("can't found '//', src uri is: %s.\n", src_uri.c_str());
            result = -1;
            return result;
        }

        position += 2;
        dest_uri->append(src_uri,0,   position) ;
        dest_uri->append(username + ":" + password + "@");
        dest_uri->append(src_uri,position, std::string::npos) ;
        */
    }

    return result;
}

/************************************************************************
**函数：ONVIF_GetStreamUri
**功能：获取设备码流地址(RTSP)
**参数：
       [in]  MediaXAddr    - 媒体服务地址
       [in]  ProfileToken  - the media profile token
**返回：
       0表明成功，非0表明失败
**备注：
************************************************************************/
int ONVIF_GetStreamUri(const char* MediaXAddr, const char* ProfileToken)
{
    int result = 0;
    struct soap* soap = nullptr;
    struct tt__StreamSetup ttStreamSetup;
    struct tt__Transport ttTransport;
    struct _trt__GetStreamUri req;
    struct _trt__GetStreamUriResponse rep;


    if (nullptr == MediaXAddr) {
        printf("[ERROR] ONVIF_GetStreamUri: MediaXAddr is null\n");
        return false;
    }
    soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
    if (nullptr == soap) {
        printf("[ERROR] ONVIF_GetStreamUri: failed to create soap context\n");
        return false;
    }
    ttStreamSetup.Stream = tt__StreamType__RTP_Unicast;
    ttStreamSetup.Transport = &ttTransport;
    ttStreamSetup.Transport->Protocol = tt__TransportProtocol__RTSP;
    ttStreamSetup.Transport->Tunnel = nullptr;
    req.StreamSetup = &ttStreamSetup;
    req.ProfileToken = (char*)(ProfileToken);

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);
    result = soap_call___trt__GetStreamUri(soap, MediaXAddr, nullptr, &req, &rep);
    SOAP_CHECK_ERROR(result, soap, "GetServices");

    if (nullptr != rep.MediaUri) {
        if (nullptr != rep.MediaUri->Uri) {
            // std::cout << rep.MediaUri->Uri << "\n";
            printf("Url:%s\n", rep.MediaUri->Uri);
        }
    }

EXIT:

    if (nullptr != soap) {
        ONVIF_soap_delete(soap);
    }

    return result;
}
void cb_discovery(char* deviceXAddr)
{
    printf("deviceXAddr:%s \n", deviceXAddr); //deviceXAddr:http://192.168.8.200:1000/onvif/device_service
    // char *mediaXAddr, profilesToken, snapUri, snapAuthUri;

    char mediaXAddr[1024] = { 0 };
    char ptzXAddr[1024] = { 0 };
    char profilesToken[1024] = { 0 };

    ONVIF_GetCapabilities(deviceXAddr, mediaXAddr, ptzXAddr);
    printf("mediaXAddr:%s \n", mediaXAddr);
    printf("ptzXAddr:%s \n", ptzXAddr);
    printf("\n\n\n");
    if (strlen(mediaXAddr) > 0) {
        ONVIF_GetProfiles(mediaXAddr, profilesToken);
    } else {
        printf("[INFO]mediaXAddr not available\n");
    }

    if (strlen(mediaXAddr) > 0 && strlen(profilesToken) > 0) {
        ONVIF_GetStreamUri(mediaXAddr, profilesToken);
    } else {
        printf("[INFO]mediaXAddr and profilesToken not available\n");
    }

    // 测试PTZ控制
    if (strlen(ptzXAddr) > 0 && strlen(profilesToken) > 0) {
        ONVIF_PTZControl(ptzXAddr, profilesToken);
    }
    else {
        printf("[INFO] PTZ service or profile not available\n");
    }

    /*    ONVIF_GetSnapshotUri(mediaXAddr, profilesToken, &snapUri);
        make_uri_withauth(snapUri, USERNAME, PASSWORD, &snapAuthUri);

        char cmd[256];
        sprintf(cmd, "wget -O %s '%s'",   "out.jpeg", snapAuthUri.c_str());                        // 使用wget下载图片
        system(cmd);*/
}

/************************************************************************
**函数：ONVIF_GetPTZNode
**功能：获取PTZ节点信息
**参数：
        [in] ptzXAddr - PTZ服务地址
        [out] nodeToken - PTZ节点令牌
**返回：
        0表明成功，非0表明失败
**备注：
************************************************************************/
int ONVIF_GetPTZNode(const char* ptzXAddr, char* nodeToken)
{
    int result = 0;
    struct soap* soap = nullptr;
    struct _tptz__GetNodes req;
    struct _tptz__GetNodesResponse rep;


    if (nullptr == ptzXAddr) {
        printf("[ERROR] ONVIF_GetPTZNode: ptzXAddr is null\n");
        return -1;
    }
    soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
    if (nullptr == soap) {
        printf("[ERROR] ONVIF_GetPTZNode: soap is null\n");
        return -1;
    }
    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);
    result = soap_call___tptz__GetNodes(soap, ptzXAddr, nullptr, &req, &rep);
    SOAP_CHECK_ERROR(result, soap, "GetNodes");

    if (rep.PTZNode && rep.__sizePTZNode > 0) {
        snprintf(nodeToken, 1024, "%s", rep.PTZNode[0].token);
        printf("ONVIF_GetPTZNode: nodeToken:%s \n", nodeToken);
    }
    else {
        printf("[ERROR] GetNodes: PTZNode is NULL or empty\n");
        result = -1;
    }

EXIT:

    if (nullptr != soap) {
        ONVIF_soap_delete(soap);
    }
    return result;
}



/************************************************************************
**函数：ONVIF_PTZContinuousMove
**功能：控制PTZ连续移动
**参数：
        [in] ptzXAddr - PTZ服务地址
        [in] profileToken - 媒体配置文件令牌
        [in] pan - 水平方向速度 (-1.0 到 1.0)
        [in] tilt - 垂直方向速度 (-1.0 到 1.0)
        [in] zoom - 缩放速度 (-1.0 到 1.0)
**返回：
        0表明成功，非0表明失败
**备注：
        pan > 0: 向右移动
        pan < 0: 向左移动
        tilt > 0: 向上移动
        tilt < 0: 向下移动
        zoom > 0: 放大
        zoom < 0: 缩小
************************************************************************/

int ONVIF_PTZContinuousMove(const char* ptzXAddr, const char* profileToken, float pan, float tilt, float zoom)
{
    int result = 0;
    struct soap* soap = nullptr;
    struct _tptz__ContinuousMove req;
    struct _tptz__ContinuousMoveResponse rep;

    // 1. 不要直接在栈上定义结构体，使用指针
    struct tt__PTZSpeed* speed = nullptr;
    struct tt__Vector2D* panTilt = nullptr;
    struct tt__Vector1D* zoomVector = nullptr;

    // 基础检查
    if (ptzXAddr == NULL || profileToken == NULL) {
        printf("Error: ptzXAddr or profileToken is NULL\n");
        return SOAP_ERR;
    }

    // 2. 初始化 soap 环境
    soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
    if (soap == nullptr) {
        printf("Error: Failed to create soap context\n");
        return SOAP_ERR;
    }

    // 3. 使用 soap_new 分配内存 (关键修复点)
    // 这会自动初始化结构体内部的指针，防止段错误
    speed = soap_new_tt__PTZSpeed(soap, -1);
    panTilt = soap_new_tt__Vector2D(soap, -1);
    zoomVector = soap_new_tt__Vector1D(soap, -1);

    if (!speed || !panTilt || !zoomVector) {
        printf("Error: Failed to allocate memory for PTZ structures\n");
        ONVIF_soap_delete(soap);
        return SOAP_ERR;
    }

    // 4. 填充数据
    // 注意：gSOAP 生成的结构体通常不需要手动 memset，soap_new 会处理
    req.ProfileToken = (char*)profileToken;
    req.Velocity = speed;

    // 赋值 PanTilt
    speed->PanTilt = panTilt;
    panTilt->x = pan;
    panTilt->y = tilt;

    // 赋值 Zoom
    speed->Zoom = zoomVector;
    zoomVector->x = zoom;

    // 5. 设置认证信息
    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

    // 6. 发送请求
    // 注意：ptzXAddr 应该是 GetCapabilities 获取到的 PTZ 服务地址
    result = soap_call___tptz__ContinuousMove(soap, ptzXAddr, nullptr, &req, &rep);

    if (result == SOAP_OK) {
        printf("ONVIF_PTZContinuousMove: Success! pan=%f, tilt=%f, zoom=%f\n", pan, tilt, zoom);
    }
    else {
        printf("ONVIF_PTZContinuousMove: Failed (Error %d)\n", result);
        // 打印详细错误信息
        soap_print_fault(soap, stderr);
    }

    // 7. 清理
    ONVIF_soap_delete(soap);
    return result;
}

/************************************************************************
**函数：ONVIF_PTZStop
**功能：停止PTZ移动
**参数：
        [in] ptzXAddr - PTZ服务地址
        [in] profileToken - 媒体配置文件令牌
**返回：
        0表明成功，非0表明失败
**备注：
************************************************************************/
int ONVIF_PTZStop(const char* ptzXAddr, const char* profileToken)
{
    int result = 0;
    struct soap* soap = nullptr;
    struct _tptz__Stop req;
    struct _tptz__StopResponse rep;
    enum xsd__boolean panTilt = xsd__boolean__true_;
    enum xsd__boolean zoom = xsd__boolean__true_;


    if (nullptr == ptzXAddr) {
        printf("[ERROR] ONVIF_PTZStop: ptzXAddr is null\n");
        return false;
    }
    if (nullptr == profileToken) {
        printf("[ERROR] ONVIF_PTZStop: profileToken is null\n");
        return false;
    }
    soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT);
    if (nullptr == soap) {
        printf("[ERROR] ONVIF_PTZStop: failed to create soap context\n");
        return false;
    }
    // 初始化请求
    req.ProfileToken = (char*)profileToken;
    req.PanTilt = &panTilt;
    req.Zoom = &zoom;

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);
    result = soap_call___tptz__Stop(soap, ptzXAddr, nullptr, &req, &rep);
    SOAP_CHECK_ERROR(result, soap, "Stop");

    printf("ONVIF_PTZStop: stopped\n");

EXIT:

    if (nullptr != soap) {
        ONVIF_soap_delete(soap);
    }
    return result;
}

/************************************************************************
**函数：ONVIF_PTZControl
**功能：PTZ控制测试函数
**参数：
        [in] ptzXAddr - PTZ服务地址
        [in] profileToken - 媒体配置文件令牌
**返回：
        0表明成功，非0表明失败
**备注：
        测试PTZ的前后左右移动和调焦
************************************************************************/
int ONVIF_PTZControl(const char* ptzXAddr, const char* profileToken)
{
    int result = 0;

    // 测试PTZ控制
    printf("\n=== Testing PTZ Control ===\n");

    // 向右移动
    printf("Moving right...\n");
    result = ONVIF_PTZContinuousMove(ptzXAddr, profileToken, 0.5, 0.0, 0.0);
    if (result != 0) {
        printf("[ERROR] Failed to move right\n");
        return result;
    }
    sleep_ms(1000); // 暂停1秒

    // 停止
    result = ONVIF_PTZStop(ptzXAddr, profileToken);
    if (result != 0) {
        printf("[ERROR] Failed to stop\n");
        return result;
    }
    sleep_ms(500); // 暂停0.5秒

    // 向左移动
    printf("Moving left...\n");
    result = ONVIF_PTZContinuousMove(ptzXAddr, profileToken, -0.5, 0.0, 0.0);
    if (result != 0) {
        printf("[ERROR] Failed to move left\n");
        return result;
    }
    sleep_ms(1000); // 暂停1秒

    // 停止
    result = ONVIF_PTZStop(ptzXAddr, profileToken);
    if (result != 0) {
        printf("[ERROR] Failed to stop\n");
        return result;
    }
    sleep_ms(500); // 暂停0.5秒

    // 向上移动
    printf("Moving up...\n");
    result = ONVIF_PTZContinuousMove(ptzXAddr, profileToken, 0.0, 0.5, 0.0);
    if (result != 0) {
        printf("[ERROR] Failed to move up\n");
        return result;
    }
    sleep_ms(1000); // 暂停1秒

    // 停止
    result = ONVIF_PTZStop(ptzXAddr, profileToken);
    if (result != 0) {
        printf("[ERROR] Failed to stop\n");
        return result;
    }
    sleep_ms(500); // 暂停0.5秒

    // 向下移动
    printf("Moving down...\n");
    result = ONVIF_PTZContinuousMove(ptzXAddr, profileToken, 0.0, -0.5, 0.0);
    if (result != 0) {
        printf("[ERROR] Failed to move down\n");
        return result;
    }
    sleep_ms(1000); // 暂停1秒

    // 停止
    result = ONVIF_PTZStop(ptzXAddr, profileToken);
    if (result != 0) {
        printf("[ERROR] Failed to stop\n");
        return result;
    }
    sleep_ms(500); // 暂停0.5秒

    // 放大
    printf("Zooming in...\n");
    result = ONVIF_PTZContinuousMove(ptzXAddr, profileToken, 0.0, 0.0, 0.5);
    if (result != 0) {
        printf("[ERROR] Failed to zoom in\n");
        return result;
    }
    sleep_ms(1000); // 暂停1秒

    // 停止
    result = ONVIF_PTZStop(ptzXAddr, profileToken);
    if (result != 0) {
        printf("[ERROR] Failed to stop\n");
        return result;
    }
    sleep_ms(500); // 暂停0.5秒

    // 缩小
    printf("Zooming out...\n");
    result = ONVIF_PTZContinuousMove(ptzXAddr, profileToken, 0.0, 0.0, -0.5);
    if (result != 0) {
        printf("[ERROR] Failed to zoom out\n");
        return result;
    }
    sleep_ms(1000); // 暂停1秒

    // 停止
    result = ONVIF_PTZStop(ptzXAddr, profileToken);
    if (result != 0) {
        printf("[ERROR] Failed to stop\n");
        return result;
    }

    printf("=== PTZ Control Test Completed ===\n");

    return result;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    // 初始化Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }
#endif

    ONVIF_DetectDevice(cb_discovery);

#ifdef _WIN32
    // 清理Winsock
    WSACleanup();
#endif

    return 0;
}