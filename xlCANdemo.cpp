/*------------------------------------------------------------------------------
 | File:
 |   xlCANdemo.C
 | Project:
 |   Sample for XL - Driver Library
 |   Example application using 'vxlapi.dll'
 |-------------------------------------------------------------------------------
 | $Author: vissj $    $Locker: $   $Revision: 60003 $
 |-------------------------------------------------------------------------------
 | Copyright (c) 2014 by Vector Informatik GmbH.  All rights reserved.
 -----------------------------------------------------------------------------*/

#if defined(_Windows) || defined(_MSC_VER) || defined (__GNUC__)
#define  STRICT
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <WinSock2.h>
#include <time.h>

#define UNUSED_PARAM(a) { a=a; }
#define T32(x) (((x&0xff)<<24)|((x&0xff00)<<8)|((x&0xff0000)>>8)|((x&0xff000000)>>24))
#define T16(x) (((x&0xff)<<8)|((x&0xff00)>>8))

#define RECEIVE_EVENT_SIZE 1                // DO NOT EDIT! Currently 1 is supported only
#define RX_QUEUE_SIZE      4096             // internal driver queue size in CAN events

#define CMD_SEND_MSG        0x01
#define CMD_RECEIVE_MSG     0x02
#define CMD_END_TEST        0x03

#include "vxlapi.h"

/////////////////////////////////////////////////////////////////////////////
// globals

char g_AppName[XL_MAX_LENGTH + 1] = "xlCANdemo"; //!< Application name which is displayed in VHWconf
XLportHandle g_xlPortHandle = XL_INVALID_PORTHANDLE; //!< Global porthandle (we use only one!)
XLdriverConfig g_xlDrvConfig;    //!< Contains the actual hardware configuration
XLaccess g_xlChannelMask = 0; //!< Global channelmask (includes all founded channels)
XLaccess g_xlPermissionMask = 0; //!< Global permissionmask (includes all founded channels)
unsigned int g_BaudRate = 1000000;                 //!< Default baudrate
int g_silent = 0;             //!< flag to visualize the message events (on/off)
unsigned int g_TimerRate = 0;                  //!< Global timerrate (to toggel)
unsigned int g_canFdSupport = 0;                 //!< Global CAN FD support flag

// tread variables
XLhandle g_hMsgEvent;             //!< notification handle for the receive queue
HANDLE g_hRXThread;                                      //!< thread handle (RX)
HANDLE g_hTXThread;                                      //!< thread handle (TX)
int g_RXThreadRun;                         //!< flag to start/stop the RX thread
int g_TXThreadRun; //!< flag to start/stop the TX thread (for the transmission burst)
int g_RXCANThreadRun;                      //!< flag to start/stop the RX thread

SOCKET sockConn = -1;
int port = 31500;

////////////////////////////////////////////////////////////////////////////
// functions (Threads)

DWORD WINAPI RxCanFdThread(PVOID par);
DWORD WINAPI RxThread(PVOID par);
DWORD WINAPI TxThread(LPVOID par);

////////////////////////////////////////////////////////////////////////////
// functions (prototypes)
void demoHelp(void);
void demoPrintConfig(void);
XLstatus demoCreateRxThread(void);

#ifdef __GNUC__
static void strncpy_s(char *strDest, size_t numberOfElements,
                const char *strSource, size_t count)
{
    UNUSED_PARAM(numberOfElements);
    strncpy(strDest, strSource, count);
}

static void sscanf_s(const char *buffer, const char *format, ...)
{
    va_list argList;
    va_start(argList, format);
    sscanf(buffer, format, argList);
}
#endif

////////////////////////////////////////////////////////////////////////////

//! demoHelp()

//! shows the program functionality
//!
////////////////////////////////////////////////////////////////////////////

void demoHelp(void)
{

    printf("\n----------------------------------------------------------\n");
    printf("-                   CanServer - HELP                     -\n");
    printf("----------------------------------------------------------\n");
    printf("- Keyboard commands:                                     -\n");
    printf("- 't'      Transmit a message                            -\n");
    printf("- 'b'      Transmit a message burst (toggle)             -\n");
    printf("- 'm'      Transmit a remote message                     -\n");
    printf("- 'g'      Request chipstate                             -\n");
    printf("- 's'      Start/Stop                                    -\n");
    printf("- 'r'      Reset clock                                   -\n");
    printf("- '+'      Select channel      (up)                      -\n");
    printf("- '-'      Select channel      (down)                    -\n");
    printf("- 'i'      Select transmit Id  (up)                      -\n");
    printf("- 'I'      Select transmit Id  (down)                    -\n");
    printf("- 'x'      Toggle extended/standard Id                   -\n");
    printf("- 'o'      Toggle output mode                            -\n");
    printf("- 'a'      Toggle timer                                  -\n");
    printf("- 'v'      Toggle logging to screen                      -\n");
    printf("- 'p'      Show hardware configuration                   -\n");
    printf("- 'y'      Trigger HW-Sync pulse                         -\n");
    printf("- 'h'      Help                                          -\n");
    printf("- 'ESC'    Exit                                          -\n");
    printf("----------------------------------------------------------\n");
    printf("- 'PH'->PortHandle; 'CM'->ChannelMask; 'PM'->Permission  -\n");
    printf("----------------------------------------------------------\n\n");

}

////////////////////////////////////////////////////////////////////////////

//! demoPrintConfig()

//! shows the actual hardware configuration
//!
////////////////////////////////////////////////////////////////////////////

void demoPrintConfig(void)
{

    unsigned int i;
    char str[100];

    printf("----------------------------------------------------------\n");
    printf("- %02d channels       Hardware Configuration               -\n",
                    g_xlDrvConfig.channelCount);
    printf("----------------------------------------------------------\n");

    for (i = 0; i < g_xlDrvConfig.channelCount; i++)
    {

        printf("- Ch:%02d, CM:0x%03I64x,",
                        g_xlDrvConfig.channel[i].channelIndex,
                        g_xlDrvConfig.channel[i].channelMask);

        strncpy_s(str, 100, g_xlDrvConfig.channel[i].name, 23);
        printf(" %23s,", str);

        memset(str, 0, sizeof(str));

        if (g_xlDrvConfig.channel[i].transceiverType != XL_TRANSCEIVER_TYPE_NONE)
        {
            strncpy_s(str, 100, g_xlDrvConfig.channel[i].transceiverName, 13);
            printf("%13s -\n", str);
        }
        else
            printf("    no Cab!   -\n");

    }

    printf("----------------------------------------------------------\n\n");

}

////////////////////////////////////////////////////////////////////////////

//! demoTransmit

//! transmit a CAN message (depending on an ID, channel)
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoTransmit(unsigned int txID, XLaccess xlChanMaskTx,
                unsigned char *data)
{
    XLstatus xlStatus;
    unsigned int messageCount = 1;
    static int cnt = 0;

    static XLevent xlEvent;

    memset(&xlEvent, 0, sizeof(xlEvent));

    xlEvent.tag = XL_TRANSMIT_MSG;
    xlEvent.tagData.msg.id = txID;
    xlEvent.tagData.msg.dlc = 8;
    xlEvent.tagData.msg.flags = 0;
    memcpy(xlEvent.tagData.msg.data, data, 8);

    xlStatus = xlCanTransmit(g_xlPortHandle, xlChanMaskTx, &messageCount,
                    &xlEvent);

    printf("- Transmit         : CM(0x%I64x), %s\n", xlChanMaskTx,
                    xlGetErrorString(xlStatus));

    return xlStatus;
}

////////////////////////////////////////////////////////////////////////////

//! demoTransmitBurst

//! transmit a message burst (also depending on an IC, channel).
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoTransmitBurst(unsigned int txID)
{
    DWORD ThreadId = 0;
    static unsigned int TXID;

    TXID = txID;

    if (!g_TXThreadRun)
    {
        printf("- print txID: %d\n", txID);

        g_hTXThread = CreateThread(0, 0x1000, TxThread, (LPVOID) &TXID, 0,
                        &ThreadId);
    }

    else
    {
        g_TXThreadRun = 0;
        Sleep(10);
        WaitForSingleObject(g_hTXThread, 10);
    }

    return XL_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////

//! demoTransmitRemote

//! transmit a remote frame
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoTransmitRemote(unsigned int txID, XLaccess xlChanMaskTx)
{
    XLstatus xlStatus;
    unsigned int messageCount = 1;

    if (g_canFdSupport)
    {
        XLcanTxEvent canTxEvt;
        unsigned int cntSent;

        memset(&canTxEvt, 0, sizeof(canTxEvt));

        canTxEvt.tag = XL_CAN_EV_TAG_TX_MSG;
        canTxEvt.transId = 0xffff;

        canTxEvt.tagData.canMsg.canId = txID;
        canTxEvt.tagData.canMsg.msgFlags = XL_CAN_TXMSG_FLAG_RTR;
        canTxEvt.tagData.canMsg.dlc = 8; //0x0f;

        xlStatus = xlCanTransmitEx(g_xlPortHandle, xlChanMaskTx, messageCount,
                        &cntSent, &canTxEvt);
    }
    else
    {
        XLevent xlEvent;

        memset(&xlEvent, 0, sizeof(xlEvent));

        xlEvent.tag = XL_TRANSMIT_MSG;
        xlEvent.tagData.msg.id = txID;
        xlEvent.tagData.msg.flags = XL_CAN_MSG_FLAG_REMOTE_FRAME;
        xlEvent.tagData.msg.dlc = 8;

        xlStatus = xlCanTransmit(g_xlPortHandle, xlChanMaskTx, &messageCount,
                        &xlEvent);
    }

    printf("- Transmit REMOTE  : CM(0x%I64x), %s\n", g_xlChannelMask,
                    xlGetErrorString(xlStatus));

    return XL_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////

//! demoStartStop

//! toggle the channel activate/deactivate
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoStartStop(int activated)
{
    XLstatus xlStatus;

    if (activated)
    {
        xlStatus = xlActivateChannel(g_xlPortHandle, g_xlChannelMask,
        XL_BUS_TYPE_CAN, XL_ACTIVATE_RESET_CLOCK);
        printf("- ActivateChannel : CM(0x%I64x), %s\n", g_xlChannelMask,
                        xlGetErrorString(xlStatus));
    }
    else
    {
        xlStatus = xlDeactivateChannel(g_xlPortHandle, g_xlChannelMask);
        printf("- DeativateChannel: CM(0x%I64x), %s\n", g_xlChannelMask,
                        xlGetErrorString(xlStatus));
    }

    return XL_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////

//! demoSetOutput

//! toggle NORMAL/SILENT mode of a CAN channel
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoSetOutput(int outputMode, XLaccess xlChanMaskTx)
{

    XLstatus xlStatus;
    char *sMode = "NORMAL";

    switch (outputMode)
    {
    case XL_OUTPUT_MODE_NORMAL:
        sMode = "NORMAL";
        break;
    case XL_OUTPUT_MODE_SILENT:
        sMode = "SILENT";
        break;
    case XL_OUTPUT_MODE_TX_OFF:
        sMode = "SILENT-TXOFF";
        break;
    }

    // to get an effect we deactivate the channel first.
    xlStatus = xlDeactivateChannel(g_xlPortHandle, g_xlChannelMask);

    xlStatus = xlCanSetChannelOutput(g_xlPortHandle, xlChanMaskTx, outputMode);
    printf("- SetChannelOutput: CM(0x%I64x), %s, %s, %d\n", xlChanMaskTx, sMode,
                    xlGetErrorString(xlStatus), outputMode);

    // and activate the channel again.
    xlStatus = xlActivateChannel(g_xlPortHandle, g_xlChannelMask,
    XL_BUS_TYPE_CAN, XL_ACTIVATE_RESET_CLOCK);

    return xlStatus;

}

////////////////////////////////////////////////////////////////////////////

//! demoCreateRxThread

//! set the notification and creates the thread.
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoCreateRxThread(void)
{
    XLstatus xlStatus = XL_ERROR;
    DWORD ThreadId = 0;

    if (g_xlPortHandle != XL_INVALID_PORTHANDLE)
    {

        // Send a event for each Msg!!!
        xlStatus = xlSetNotification(g_xlPortHandle, &g_hMsgEvent, 1);

        if (g_canFdSupport)
        {
            g_hRXThread = CreateThread(0, 0x1000, RxCanFdThread, (LPVOID) 0, 0,
                            &ThreadId);
        }
        else
        {
            g_hRXThread = CreateThread(0, 0x1000, RxThread, (LPVOID) 0, 0,
                            &ThreadId);
        }

    }
    return xlStatus;
}

////////////////////////////////////////////////////////////////////////////

//! demoInitDriver

//! initializes the driver with one port and all founded channels which
//! have a connected CAN cab/piggy.
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoInitDriver(XLaccess *pxlChannelMaskTx,
                unsigned int *pxlChannelIndex)
{

    XLstatus xlStatus;
    unsigned int i;
    XLaccess xlChannelMaskFd = 0;

    // ------------------------------------
    // open the driver
    // ------------------------------------
    xlStatus = xlOpenDriver();

    // ------------------------------------
    // get/print the hardware configuration
    // ------------------------------------
    if (XL_SUCCESS == xlStatus)
    {
        xlStatus = xlGetDriverConfig(&g_xlDrvConfig);
    }

    if (XL_SUCCESS == xlStatus)
    {
        demoPrintConfig();

        printf(
                        "Usage: xlCANdemo <BaudRate> <ApplicationName> <Identifier>\n\n");

        // ------------------------------------
        // select the wanted channels
        // ------------------------------------
        g_xlChannelMask = 0;
        for (i = 0; i < g_xlDrvConfig.channelCount; i++)
        {

            // we take all hardware we found and supports CAN
            if (g_xlDrvConfig.channel[i].channelBusCapabilities
                            & XL_BUS_ACTIVE_CAP_CAN)
            {

                if (!*pxlChannelMaskTx)
                {
                    *pxlChannelMaskTx = g_xlDrvConfig.channel[i].channelMask;
                    *pxlChannelIndex = g_xlDrvConfig.channel[i].channelIndex;
                }

                // check if we can use CAN FD - the virtual CAN driver supports CAN-FD, but we don't use it
                if (g_xlDrvConfig.channel[i].channelCapabilities
                                & XL_CHANNEL_FLAG_CANFD_ISO_SUPPORT)
                {
                    xlChannelMaskFd |= g_xlDrvConfig.channel[i].channelMask;
                }
                else
                {
                    g_xlChannelMask |= g_xlDrvConfig.channel[i].channelMask;
                }

            }
        }

        // if we found a CAN FD supported channel - we use it.
        if (xlChannelMaskFd)
        {
            g_xlChannelMask = xlChannelMaskFd;
            printf("- Use CAN-FD for   : CM=0x%I64x\n", g_xlChannelMask);
            g_canFdSupport = 1;
        }

        if (!g_xlChannelMask)
        {
            printf(
                            "ERROR: no available channels found! (e.g. no CANcabs...)\n\n");
            xlStatus = XL_ERROR;
        }
    }

    g_xlPermissionMask = g_xlChannelMask;

    // ------------------------------------
    // open ONE port including all channels
    // ------------------------------------
    if (XL_SUCCESS == xlStatus)
    {
        {
            xlStatus = xlOpenPort(&g_xlPortHandle, g_AppName, g_xlChannelMask,
                            &g_xlPermissionMask, RX_QUEUE_SIZE,
                            XL_INTERFACE_VERSION, XL_BUS_TYPE_CAN);

        }
        printf("- OpenPort         : CM=0x%I64x, PH=0x%02X, PM=0x%I64x, %s\n",
                        g_xlChannelMask, g_xlPortHandle, g_xlPermissionMask,
                        xlGetErrorString(xlStatus));

    }

    if ((XL_SUCCESS == xlStatus) && (XL_INVALID_PORTHANDLE != g_xlPortHandle))
    {

        // ------------------------------------
        // if we have permission we set the
        // bus parameters (baudrate)
        // ------------------------------------
        if (g_xlChannelMask == g_xlPermissionMask)
        {
            {
                xlStatus = xlCanSetChannelBitrate(g_xlPortHandle,
                                g_xlChannelMask, g_BaudRate);
                printf("- SetChannelBitrate: baudr.=%u, %s\n", g_BaudRate,
                                xlGetErrorString(xlStatus));
            }
        }
        else
        {
            printf("-                  : we have NO init access!\n");
        }
    }
    else
    {

        xlClosePort(g_xlPortHandle);
        g_xlPortHandle = XL_INVALID_PORTHANDLE;
        xlStatus = XL_ERROR;
    }

    return xlStatus;

}

////////////////////////////////////////////////////////////////////////////

//! demoCleanUp()

//! close the port and the driver
//!
////////////////////////////////////////////////////////////////////////////

static XLstatus demoCleanUp(void)
{
    XLstatus xlStatus;

    if (g_xlPortHandle != XL_INVALID_PORTHANDLE)
    {
        xlStatus = xlClosePort(g_xlPortHandle);
        printf("- ClosePort        : PH(0x%x), %s\n", g_xlPortHandle,
                        xlGetErrorString(xlStatus));
    }

    g_xlPortHandle = XL_INVALID_PORTHANDLE;
    xlCloseDriver();

    return XL_SUCCESS;    // No error handling
}

int demoStartServer(void)
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("Failed to load Winsock");
        return -1;
    }
    //创建用于监听的套接字
    SOCKET sockSrv = socket(AF_INET, SOCK_STREAM, 0);

    SOCKADDR_IN addrSrv;
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons(port); //1024以上的端口号
    addrSrv.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

    int retVal = bind(sockSrv, (LPSOCKADDR) &addrSrv, sizeof(SOCKADDR_IN));
    if (retVal == SOCKET_ERROR)
    {
        printf("Failed bind:%d\n", WSAGetLastError());
        return -1 ;
    }

    if (listen(sockSrv, 10) == SOCKET_ERROR)
    {
        printf("Listen failed:%d", WSAGetLastError());
        return -1;
    }

    SOCKADDR_IN addrClient;
    int len = sizeof(SOCKADDR);

    while (1)
    {
        //等待客户请求到来
        printf("\nStart server at port:%d successfully.", port);

        printf("\nWaiting for client to connect...");
        sockConn = accept(sockSrv, (SOCKADDR *) &addrClient, &len);
        if (sockConn == SOCKET_ERROR)
        {
            printf("Accept failed:%d", WSAGetLastError());

        }

        printf("Accept client IP:[%s]\n", inet_ntoa(addrClient.sin_addr));
        break;

    }
    return 0;
}

typedef struct cmd_tag
{
    unsigned short type;
    unsigned short msg_id;
    unsigned char data[8];
} cmd_t;

////////////////////////////////////////////////////////////////////////////

//! main

//! 
//!
////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    XLstatus xlStatus;
    XLaccess xlChanMaskTx = 0;

    int stop = 0;
    int activated = 0;
    int c;
    unsigned int xlChanIndex = 0;
    unsigned int txID = 0x01;
    int outputMode = XL_OUTPUT_MODE_NORMAL;



    printf("----------------------------------------------------------\n");
    printf("- xlCANdemo - Test Application for XL Family Driver API  -\n");
    printf("-             Vector Informatik GmbH,  " __DATE__"       -\n");
#ifdef WIN64
    printf("-             - 64bit Version -                          -\n");
#endif
    printf("----------------------------------------------------------\n");

    // ------------------------------------
    // commandline may specify application
    // name and baudrate
    // ------------------------------------
    if (argc > 1)
    {
        g_BaudRate = atoi(argv[1]);
        if (g_BaudRate)
        {
            printf("Baudrate = %u\n", g_BaudRate);
            argc--;
            argv++;
        }
    }
    if (argc > 1)
    {
        strncpy_s(g_AppName, XL_MAX_APPNAME, argv[1], XL_MAX_APPNAME);
        g_AppName[XL_MAX_APPNAME] = 0;
        printf("AppName = %s\n", g_AppName);
        argc--;
        argv++;
    }
    if (argc > 1)
    {
        sscanf_s(argv[1], "%lx", &txID);
        if (txID)
        {
            printf("TX ID = %lx\n", txID);
        }
    }

    demoStartServer();

    // ------------------------------------
    // initialize the driver structures
    // for the application
    // ------------------------------------
    xlStatus = demoInitDriver(&xlChanMaskTx, &xlChanIndex);
    printf("- Init             : %s\n", xlGetErrorString(xlStatus));

    if (XL_SUCCESS == xlStatus)
    {
        // ------------------------------------
        // create the RX thread to read the
        // messages
        // ------------------------------------
        xlStatus = demoCreateRxThread();
        printf("- Create RX thread : %s\n", xlGetErrorString(xlStatus));
    }

    if (XL_SUCCESS == xlStatus)
    {
        // ------------------------------------
        // go with all selected channels on bus
        // ------------------------------------
        xlStatus = xlActivateChannel(g_xlPortHandle, g_xlChannelMask,
        XL_BUS_TYPE_CAN, XL_ACTIVATE_RESET_CLOCK);
        printf("- ActivateChannel  : CM=0x%I64x, %s\n", g_xlChannelMask,
                        xlGetErrorString(xlStatus));
        if (xlStatus == XL_SUCCESS) activated = 1;
    }

    printf("\n: Press <h> for help - actual channel Ch=%d, CM=0x%02I64x\n",
                    xlChanIndex, xlChanMaskTx);





    printf("client connect successfully.");

    // ------------------------------------
    // parse the key - commands
    // ------------------------------------
    while (stop == 0)
    {
        char recvBuf[100];
        int num = 0;

        memset(recvBuf, 0, sizeof(recvBuf));
        //      //接收数据
        num = recv(sockConn, recvBuf, sizeof(recvBuf), 0);
        if (num > 0)
        {
            printf("%s\n", recvBuf);
            cmd_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            memcpy(&cmd, recvBuf, sizeof(cmd));
            cmd.type = T16(cmd.type);
            cmd.msg_id = T16(cmd.msg_id);
            switch (cmd.type)
            {
            case CMD_SEND_MSG:
                demoTransmit(cmd.msg_id, xlChanMaskTx, cmd.data);
                break;
            case CMD_END_TEST:
                stop = 1;
                break;
            default:
                break;
            }

        }

    }                                                         // end while

    if ((XL_SUCCESS != xlStatus) && activated)
    {
        xlStatus = xlDeactivateChannel(g_xlPortHandle, g_xlChannelMask);
        printf("- DeactivateChannel: CM(0x%I64x), %s\n", g_xlChannelMask,
                        xlGetErrorString(xlStatus));
    }
    demoCleanUp();

    return (0);
}                                                  // end main()

////////////////////////////////////////////////////////////////////////////

//! TxThread

//! 
//!
////////////////////////////////////////////////////////////////////////////

DWORD WINAPI TxThread(LPVOID par)
{
    XLstatus xlStatus = XL_SUCCESS;
    unsigned int n = 1;
    unsigned int TxID = *(unsigned int*) par;
    XLcanTxEvent canTxEvt;
    XLevent xlEvent;
    unsigned int cntSent;

    if (g_canFdSupport)
    {

        unsigned int i;

        memset(&canTxEvt, 0, sizeof(canTxEvt));
        canTxEvt.tag = XL_CAN_EV_TAG_TX_MSG;
        canTxEvt.transId = 0xffff;

        canTxEvt.tagData.canMsg.canId = TxID;
        canTxEvt.tagData.canMsg.msgFlags = XL_CAN_TXMSG_FLAG_EDL
                        | XL_CAN_TXMSG_FLAG_BRS;
        canTxEvt.tagData.canMsg.dlc = 8;                             //0x0f;

        for (i = 1; i < XL_CAN_MAX_DATA_LEN; ++i)
        {
            canTxEvt.tagData.canMsg.data[i] = (unsigned char) i - 1;
        }
    }
    else
    {

        memset(&xlEvent, 0, sizeof(xlEvent));

        xlEvent.tag = XL_TRANSMIT_MSG;
        xlEvent.tagData.msg.id = TxID;
        xlEvent.tagData.msg.dlc = 8;
        xlEvent.tagData.msg.flags = 0;
        xlEvent.tagData.msg.data[0] = 1;
        xlEvent.tagData.msg.data[1] = 2;
        xlEvent.tagData.msg.data[2] = 3;
        xlEvent.tagData.msg.data[3] = 4;
        xlEvent.tagData.msg.data[4] = 5;
        xlEvent.tagData.msg.data[5] = 6;
        xlEvent.tagData.msg.data[6] = 7;
        xlEvent.tagData.msg.data[7] = 8;

    }

    g_TXThreadRun = 1;

    while (g_TXThreadRun && XL_SUCCESS == xlStatus)
    {

        if (g_canFdSupport)
        {
            ++canTxEvt.tagData.canMsg.data[0];
            xlStatus = xlCanTransmitEx(g_xlPortHandle, g_xlChannelMask, n,
                            &cntSent, &canTxEvt);
        }
        else
        {
            ++xlEvent.tagData.msg.data[0];
            xlStatus = xlCanTransmit(g_xlPortHandle, g_xlChannelMask, &n,
                            &xlEvent);
        }

        Sleep(10);

    }

    if (XL_SUCCESS != xlStatus)
    {
        printf("Error xlCanTransmit:%s\n", xlGetErrorString(xlStatus));
    }
    return NO_ERROR;

}

///////////////////////////////////////////////////////////////////////////

//! RxThread

//! thread to readout the message queue and parse the incoming messages
//!
////////////////////////////////////////////////////////////////////////////

DWORD WINAPI RxThread(LPVOID par)
{
    XLstatus xlStatus;

    unsigned int msgsrx = RECEIVE_EVENT_SIZE;
    XLevent xlEvent;

    UNUSED_PARAM(par);

    g_RXThreadRun = 1;

    while (g_RXThreadRun)
    {

        WaitForSingleObject(g_hMsgEvent, 10);

        xlStatus = XL_SUCCESS;

        while (!xlStatus)
        {

            msgsrx = RECEIVE_EVENT_SIZE;

            xlStatus = xlReceive(g_xlPortHandle, &msgsrx, &xlEvent);
            if (xlStatus != XL_ERR_QUEUE_IS_EMPTY)
            {

                if(sockConn != -1)
                {
                    send(sockConn, xlGetEventString(&xlEvent), 84, 0);
                }
            }
        }

    }
    return NO_ERROR;
}

///////////////////////////////////////////////////////////////////////////

//! RxCANThread

//! thread to read the message queue and parse the incoming messages
//!
////////////////////////////////////////////////////////////////////////////
DWORD WINAPI RxCanFdThread(LPVOID par)
{
    XLstatus xlStatus = XL_SUCCESS;
    DWORD rc;
    XLcanRxEvent xlCanRxEvt;

    UNUSED_PARAM(par);

    g_RXCANThreadRun = 1;

    while (g_RXCANThreadRun)
    {
        rc = WaitForSingleObject(g_hMsgEvent, 10);
        if (rc != WAIT_OBJECT_0) continue;

        do
        {
            xlStatus = xlCanReceive(g_xlPortHandle, &xlCanRxEvt);
            if (xlStatus == XL_ERR_QUEUE_IS_EMPTY)
            {
                break;
            }
            if (!g_silent)
            {
                printf("%s\n", xlCanGetEventString(&xlCanRxEvt));
            }

        }
        while (XL_SUCCESS == xlStatus);
    }

    return (NO_ERROR);
} // RxCanFdThread

