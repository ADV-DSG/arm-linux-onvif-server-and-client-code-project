#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>


//time
#define ONVIF_TIME_OUT "60000"      //60s
//port
#define ONVIF_UDP_PORT 3702
#define ONVIF_TCP_PORT 5000
//ip
#define ONVIF_UDP_IP "239.255.255.250"
// #define ONVIF_TCP_IP "192.168.8.217"
//frame size
#define ONVIF_FRAME_WIDTH 640
#define ONVIF_FRAME_HEIGHT 480


extern char g_local_ip[256];
extern char g_device_xaddr[512];
extern char g_media_xaddr[512];
extern char g_ptz_xaddr[512];
extern char g_rtsp_uri[512];
