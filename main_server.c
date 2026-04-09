#include "soapH.h"
#include "soapStub.h"
#include "wsaapi.h"
#include "wsdd.nsmap"
#include "wsseapi.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "common.h"

#ifdef _WIN32
// Windows compatibility macros
#define sleep(x) Sleep((x)*1000)
#endif



#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <Mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

// SIO_UDP_CONNRESET definition if not available
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#else
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#endif








char g_local_ip[256] = "127.0.0.1";
char g_device_xaddr[512];
char g_media_xaddr[512];
char g_ptz_xaddr[512];
char g_rtsp_uri[512];





struct soap *ONVIF_soap_new(int timeout)
{
    struct soap *soap = NULL;

    assert(NULL != (soap = soap_new()));

    soap_set_namespaces(soap, namespaces);
    soap->recv_timeout = timeout;
    soap->send_timeout = timeout;
    soap->connect_timeout = timeout;

#if defined(__linux__) || defined(__linux)
    soap->socket_flags = MSG_NOSIGNAL;
#endif

    soap_set_mode(soap, SOAP_C_UTFSTRING);

    // 注册 wsse 插件
    if (soap_register_plugin(soap, soap_wsse)) {
        printf("[ERROR] Failed to register wsse plugin\n");
        soap_done(soap);
        soap_free(soap);
        return NULL;
    }

    return soap;
}

void ONVIF_soap_delete(struct soap *soap)
{
    if (soap) {
        soap_destroy(soap);
        soap_end(soap);
        soap_done(soap);
        soap_free(soap);
    }
}

void get_local_ip(char *ip_buffer, size_t buffer_size)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sock < 0) {
        strncpy(ip_buffer, "127.0.0.1", buffer_size);
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        if (getsockname(sock, (struct sockaddr *)&addr, &addr_len) == 0) {
            inet_ntop(AF_INET, &addr.sin_addr, ip_buffer, buffer_size);
        } else {
            strncpy(ip_buffer, "127.0.0.1", buffer_size);
        }
    } else {
        strncpy(ip_buffer, "127.0.0.1", buffer_size);
    }

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

// 检查IP地址是否有效（非127.0.0.1）
static int is_valid_ip(const char* ip) {
    return (ip != NULL && strlen(ip) > 0 && strcmp(ip, "127.0.0.1") != 0);
}

// 等待网卡启动并获取有效的IP地址
static void wait_for_valid_ip() {
    char ip[256] = "";
    int attempts = 0;
    const int max_attempts = 60; // 最多等待300秒（5分钟）
    
    printf("Waiting for network interface to start...\n");
    
    // while (attempts < max_attempts) {
    while (1) {
        get_local_ip(ip, sizeof(ip));
        
        if (is_valid_ip(ip)) {
            printf("Network interface started successfully. IP: %s\n", ip);
            strncpy(g_local_ip, ip, sizeof(g_local_ip));
            return;
        }
        
        attempts++;
        printf("Attempt %d/%d: Waiting for network...\n", attempts, max_attempts);
        sleep(3); // 每3秒检查一次
    }
    
    // 如果超时，使用127.0.0.1作为 fallback
    printf("WARNING: Network interface not started within timeout. Using 127.0.0.1\n");
    strncpy(g_local_ip, "127.0.0.1", sizeof(g_local_ip));
}




static void *wsdd_server_thread(void *arg) {

    printf("[%s][%d][%s][%s] start \n", __FILE__, __LINE__, __TIME__, __func__);
    
    struct soap UDPserverSoap = {0};
    struct ip_mreq mcast;

    soap_init1(&UDPserverSoap, SOAP_IO_UDP | SOAP_XML_IGNORENS);
    soap_set_namespaces(&UDPserverSoap,  namespaces);

    printf("[%s][%d][%s][%s] UDPserverSoap.version = %d \n", __FILE__, __LINE__, __TIME__, __func__, UDPserverSoap.version);

    int m = soap_bind(&UDPserverSoap, NULL, ONVIF_UDP_PORT, 10);
    if(!soap_valid_socket(m))
    {
        soap_print_fault(&UDPserverSoap, stderr);
        exit(1);
    }
    printf("socket bind success %d\n", m);

    mcast.imr_multiaddr.s_addr = inet_addr(ONVIF_UDP_IP);
    mcast.imr_interface.s_addr = htonl(INADDR_ANY);
    //IP_ADD_MEMBERSHIP用于加入某个多播组，之后就可以向这个多播组发送数据或者从多播组接收数据
    if(setsockopt(UDPserverSoap.master, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mcast, sizeof(mcast)) < 0)
    {
        printf("setsockopt error! error code = %d,err string = %s\n",errno,strerror(errno));
        return 0;
    }
    
    int fd = -1;
    while(1)
    {
        printf("udp socket connect %d\n", fd);
        fd = soap_accept(&UDPserverSoap);
        if (!soap_valid_socket(fd)) {
			soap_print_fault(&UDPserverSoap, stderr);
			exit(1);
		}
		

        if( soap_serve(&UDPserverSoap) != SOAP_OK )
        {
            soap_print_fault(&UDPserverSoap, stderr);
            printf("soap_print_fault\n");
        }

        printf("UDP IP = %u.%u.%u.%u\n", ((UDPserverSoap.ip)>>24)&0xFF, ((UDPserverSoap.ip)>>16)&0xFF, ((UDPserverSoap.ip)>>8)&0xFF,(UDPserverSoap.ip)&0xFF);
        soap_destroy(&UDPserverSoap);
        soap_end(&UDPserverSoap);
    }
    //分离运行时的环境
    soap_done(&UDPserverSoap);
    pthread_exit(0);
}	


int main(int argc, char **argv)
{
    struct soap *soap_tcp = NULL;
    int port = ONVIF_TCP_PORT;
#ifdef _WIN32
    HANDLE wsdd_thread;
    WSADATA wsaData;
    
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Failed to initialize Winsock\n");
        return -1;
    }
#else
    pthread_t wsdd_thread;
#endif

    // 等待网卡启动并获取有效的IP地址
    wait_for_valid_ip();
    
    snprintf(g_device_xaddr, sizeof(g_device_xaddr), 
             "http://%s:%d/onvif/device_service", g_local_ip, port);
    snprintf(g_media_xaddr, sizeof(g_media_xaddr), 
             "http://%s:%d/onvif/media_service", g_local_ip, port);
    snprintf(g_ptz_xaddr, sizeof(g_ptz_xaddr), 
             "http://%s:%d/onvif/ptz_service", g_local_ip, port);

    snprintf(g_rtsp_uri, sizeof(g_rtsp_uri), 
             "rtsp://%s:11554/live", g_local_ip);
    // snprintf(g_rtsp_uri, sizeof(g_rtsp_uri), 
    //          "rtsp://%s:11554/live", "192.168.8.200");

    printf("Local IP address: %s\n", g_local_ip);
    printf("Device XAddr: %s\n", g_device_xaddr);
    printf("Media XAddr: %s\n", g_media_xaddr);
    printf("PTZ XAddr: %s\n", g_ptz_xaddr);
    printf("RTSP URI: %s\n", g_rtsp_uri);

    soap_tcp = ONVIF_soap_new(10);
    if (!soap_tcp) {
        printf("Failed to create TCP soap environment\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    soap_tcp->bind_flags = SO_REUSEADDR;

    if (soap_bind(soap_tcp, NULL, port, 100) < 0) {
        printf("Failed to bind to TCP port %d: %s\n", port, *soap_faultstring(soap_tcp));
        ONVIF_soap_delete(soap_tcp);
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    printf("ONVIF Server started on TCP port %d\n", port);
    printf("You can access the service at: %s\n", g_device_xaddr);

#ifdef _WIN32
    wsdd_thread = CreateThread(NULL, 0, wsdd_server_thread, NULL, 0, NULL);
    if (wsdd_thread == NULL) {
        printf("Failed to create WS-Discovery thread: %d\n", GetLastError());
        ONVIF_soap_delete(soap_tcp);
        WSACleanup();
        return -1;
    }
#else
    if (pthread_create(&wsdd_thread, NULL, wsdd_server_thread, NULL) != 0) {
        printf("Failed to create WS-Discovery thread\n");
        ONVIF_soap_delete(soap_tcp);
        return -1;
    }
#endif

    printf("WS-Discovery thread started\n");
    printf("Server is running. Press Ctrl+C to stop.\n\n");


    while (1) {
        printf("[DEBUG] Waiting for TCP connection...\n");
        
        // 接受连接
        if (soap_accept(soap_tcp) < 0) {
            printf("[ERROR] Failed to accept TCP connection: %s\n", *soap_faultstring(soap_tcp));
            continue;
        }
        

        uint32_t  correct_ip = ntohl(soap_tcp->ip); 
        printf("[DEBUG] TCP connection accepted from %s:%d\n", 
                inet_ntoa(*(struct in_addr*)&correct_ip),  (int)soap_tcp->port);




        if( soap_serve(soap_tcp) != SOAP_OK )
        {
            soap_print_fault(soap_tcp, stderr);
            printf("soap_print_fault\n");
        }  




        // 清理soap环境
        printf("[DEBUG] Cleaning up soap environment\n");
        soap_destroy(soap_tcp);
        soap_end(soap_tcp);
        printf("[DEBUG] Soap environment cleaned up\n");

    }





#ifdef _WIN32
    TerminateThread(wsdd_thread, 0);
    CloseHandle(wsdd_thread);
    WSACleanup();
#else
    pthread_cancel(wsdd_thread);
    pthread_join(wsdd_thread, NULL);
#endif

    ONVIF_soap_delete(soap_tcp);
    return 0;
}
