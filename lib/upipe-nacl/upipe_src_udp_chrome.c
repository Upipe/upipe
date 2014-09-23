/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Xavier Boulet
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_common.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_clock.h>
#include <upipe/uclock_std.h>
#include <upipe/ubuf_pic.h>
#include <upipe/uref_dump.h>
#include <upipe/uref.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_source_read_size.h>
#include <upipe-av/upipe_av_pixfmt.h>
#include <upipe/uqueue.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <upipe-nacl/upipe_src_udp_chrome.h>

#define UQUEUE_SIZE 255
#define r_size  65536

struct upipe_src_udp_chrome {
    /** refcount management structure */
    struct urefcount urefcount;
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** pipe acting as output */
    struct upipe *output;
    /** output flow */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** public upipe structure */
    struct upipe upipe;
    /** PPAPI Interfaces & Resources*/
    PPB_UDPSocket* udp_socket_interface;
    PPB_NetAddress* net_address_interface;
    PPB_MessageLoop* message_loop_interface;

    PP_Resource addr;
    PP_Resource loop;
    PP_Resource udpSocket;
    const char *uri;
    struct uqueue bufferUDP;
    struct udp_data udpData;
    /** extra data for the queue structure */
    uint8_t extra[];
};

UPIPE_HELPER_UPIPE(upipe_src_udp_chrome, upipe, UPIPE_SRC_UDP_CHROME_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_src_udp_chrome, urefcount, upipe_src_udp_chrome_free);
UPIPE_HELPER_VOID(upipe_src_udp_chrome);
UPIPE_HELPER_UREF_MGR(upipe_src_udp_chrome, uref_mgr);
UPIPE_HELPER_UBUF_MGR(upipe_src_udp_chrome, ubuf_mgr, flow_def);
UPIPE_HELPER_OUTPUT(upipe_src_udp_chrome, output, flow_def, flow_def_sent);
UPIPE_HELPER_UPUMP_MGR(upipe_src_udp_chrome, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_src_udp_chrome, upump, upump_mgr);
UPIPE_HELPER_UCLOCK(upipe_src_udp_chrome, uclock);

/* Implement htons locally. */
static uint16_t Htons(uint16_t hostshort) {
    uint8_t result_bytes[2];
    result_bytes[0] = (uint8_t) ((hostshort >> 8) & 0xFF);
    result_bytes[1] = (uint8_t) (hostshort & 0xFF);

    uint16_t result;
    memcpy(&result, result_bytes, 2);
    return result;
}

/* Create a struct PP_NetAddress_IPv4 */
static struct PP_NetAddress_IPv4 createIPv4(uint16_t port, in_addr_t addr)
{
    struct PP_NetAddress_IPv4 addressIPv4;
    addressIPv4.port = htons(port);
    uint32_t a = htonl(addr);
    addressIPv4.addr[0] = a >> 24;
    addressIPv4.addr[1] = a >> 16;
    addressIPv4.addr[2] = a >> 8;
    addressIPv4.addr[3] = a;
    return addressIPv4;
}

/** @internal @This read data.
 *
 * @param user_data is a struct udp_data
 */
void startCallBack_UDP(void* user_data, int32_t result) {
    struct udp_data* data = (struct udp_data*)(user_data); 
    PPB_UDPSocket* udp_socket_interface = (PPB_UDPSocket*)PSGetInterface(PPB_UDPSOCKET_INTERFACE);
    struct PP_CompletionCallback cb_onReceive = PP_MakeCompletionCallback(startCallBack_UDP,data);
    if(result < 0)
    {
        printf("error");
        return;
    }
    if(result == 0)
    {
        data->buffer = malloc(r_size*sizeof(uint8_t));
        udp_socket_interface->RecvFrom(data->udp_socket,(char*)(data->buffer+sizeof(int)),r_size,NULL,cb_onReceive); 
    }
    if(result >0)
    {
        memcpy(data->buffer,&result,sizeof(int));
        if(unlikely(!uqueue_push(data->bufferUDP, data->buffer)))   {
            free(data->buffer);
        }
        data->buffer = malloc(r_size*sizeof(uint8_t));
        udp_socket_interface->RecvFrom(data->udp_socket,(char*)(data->buffer+sizeof(int)),r_size,NULL,cb_onReceive);
    }    
    return ;
}

/** @internal @This allocates a filter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe* upipe_src_udp_chrome_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe, uint32_t signature, va_list args)
{
    struct upipe_src_udp_chrome *upipe_src_udp_chrome = malloc(sizeof(struct upipe_src_udp_chrome)+uqueue_sizeof(UQUEUE_SIZE));
    struct upipe *upipe = upipe_src_udp_chrome_to_upipe(upipe_src_udp_chrome);
    upipe_init(upipe, mgr, uprobe);
    if (unlikely(upipe == NULL)){
        return NULL;
    }
    upipe_src_udp_chrome_init_urefcount(upipe);
    upipe_src_udp_chrome_init_uref_mgr(upipe);;
    upipe_src_udp_chrome_init_ubuf_mgr(upipe);
    upipe_src_udp_chrome_init_output(upipe);
    upipe_src_udp_chrome_init_upump_mgr(upipe);
    upipe_src_udp_chrome_init_upump(upipe);
    upipe_src_udp_chrome_init_uclock(upipe);
    upipe_src_udp_chrome_check_uref_mgr(upipe);
    upipe_src_udp_chrome_check_upump_mgr(upipe);
    upipe_src_udp_chrome->udp_socket_interface = (PPB_UDPSocket*)PSGetInterface(PPB_UDPSOCKET_INTERFACE);
    upipe_src_udp_chrome->message_loop_interface = (PPB_MessageLoop*)PSGetInterface(PPB_MESSAGELOOP_INTERFACE);
     upipe_src_udp_chrome->net_address_interface =(PPB_NetAddress*)PSGetInterface(PPB_NETADDRESS_INTERFACE);
    upipe_src_udp_chrome->loop = va_arg(args,PP_Resource);
    if(unlikely(!uqueue_init(&upipe_src_udp_chrome->bufferUDP, UQUEUE_SIZE, upipe_src_udp_chrome->extra)))
    {
        free(upipe_src_udp_chrome);
        return NULL;
    }
    struct uref *flow_def = uref_block_flow_alloc_def(upipe_src_udp_chrome->uref_mgr,NULL);
    upipe_src_udp_chrome_store_flow_def(upipe, flow_def);
    upipe_src_udp_chrome_check_ubuf_mgr(upipe);
    
    /*Data for message_loop*/
    upipe_src_udp_chrome->udpData.bufferUDP = &(upipe_src_udp_chrome->bufferUDP);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This outputs data.*/
static void upipe_src_udp_chrome_idler(struct upump *upump)
{

    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_src_udp_chrome *upipe_src_udp_chrome = upipe_src_udp_chrome_from_upipe(upipe);
    if(unlikely(uqueue_length(&upipe_src_udp_chrome->bufferUDP) == 0))
    {
        return;
    }
    uint64_t now = uclock_now(upipe_src_udp_chrome->uclock);
    uint8_t* buffer = uqueue_pop(&upipe_src_udp_chrome->bufferUDP,uint8_t*);
    int read_size;
    memcpy(&read_size,buffer,sizeof(int));
    uint8_t *buffer2;
    struct uref *uref = uref_block_alloc(upipe_src_udp_chrome->uref_mgr,upipe_src_udp_chrome->ubuf_mgr,read_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uref_clock_set_cr_sys(uref, now);
 
    if (unlikely(!ubase_check(uref_block_write(uref, 0, &read_size,&buffer2)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uref_block_unmap(uref, 0);
    memcpy(buffer2,buffer+sizeof(int),read_size);
    free(buffer);
    upipe_src_udp_chrome_output(upipe, uref, &upipe_src_udp_chrome->upump);
    return;
 }

/** @internal @This asks to open the given socket.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the socket
 * @return an error code
 */
static int upipe_src_udp_chrome_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_src_udp_chrome *upipe_src_udp_chrome = upipe_src_udp_chrome_from_upipe(upipe);

    if (unlikely(upipe_src_udp_chrome->udpSocket != 0)) {
        upipe_src_udp_chrome->udp_socket_interface->Close(upipe_src_udp_chrome->udpSocket);
        upipe_src_udp_chrome->udpSocket = 0;
    }
    free((char*)(upipe_src_udp_chrome->uri));
    upipe_src_udp_chrome->uri = NULL;
    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;
    upipe_src_udp_chrome->uri = strdup(uri);

    struct in_addr multicast_addr, source_addr;
    uint16_t port_number = 0;
    multicast_addr.s_addr = source_addr.s_addr = INADDR_ANY;

    char *string = strdup(uri);
    if (string == NULL)
        return UBASE_ERR_ALLOC;
    char *source = string;
    char *multicast = strchr(source, '@');

    if (multicast != NULL) {
        *multicast++ = '\0';
        char *port = strchr(multicast, ':');
        if (port != NULL) {
            *port++ = '\0';
            port_number = strtoul(port, &port, 10);
            if (*port != '\0') {
                upipe_err_va(upipe, "invalid port");
                goto upipe_src_udp_chrome_set_uri_err;
            }
        }
        if (!inet_aton(multicast, &multicast_addr)) {
            upipe_err_va(upipe, "invalid multicast address");
            goto upipe_src_udp_chrome_set_uri_err;
        }
    }
    if (!inet_aton(source, &source_addr)) {
        upipe_err_va(upipe, "invalid source address");
        goto upipe_src_udp_chrome_set_uri_err;
    }
    free(string);

    /*create socket + bind*/
    struct PP_NetAddress_IPv4 addressIPv4 = createIPv4(port_number, multicast_addr.s_addr);
    upipe_src_udp_chrome->addr = upipe_src_udp_chrome->net_address_interface->CreateFromIPv4Address(PSGetInstanceId(),&addressIPv4);
    upipe_src_udp_chrome->udpSocket = upipe_src_udp_chrome->udp_socket_interface->Create(PSGetInstanceId());

    struct PP_Var var;
    var.type = PP_VARTYPE_BOOL;
    var.value.as_bool = true;
    struct PP_CompletionCallback cb_onConnect = PP_BlockUntilComplete();
    upipe_src_udp_chrome->udp_socket_interface->SetOption(upipe_src_udp_chrome->udpSocket, PP_UDPSOCKET_OPTION_ADDRESS_REUSE, var, cb_onConnect);

    if(upipe_src_udp_chrome->udp_socket_interface->Bind(upipe_src_udp_chrome->udpSocket,upipe_src_udp_chrome->addr,cb_onConnect) != PP_OK) 
    {
        printf("Binding error\n");
        return UBASE_ERR_EXTERNAL;
    }

    /*Data&Postwork for message_loop*/
    upipe_src_udp_chrome->udpData.udp_socket = upipe_src_udp_chrome->udpSocket;
    upipe_src_udp_chrome->udpData.buffer = NULL;
    
    struct PP_CompletionCallback startCB_UDP = PP_MakeCompletionCallback(startCallBack_UDP,&(upipe_src_udp_chrome->udpData));
    if(upipe_src_udp_chrome->message_loop_interface->PostWork(upipe_src_udp_chrome->loop, startCB_UDP,0)!=PP_OK)
    {
        printf("Postwork error\n");
        return UBASE_ERR_EXTERNAL;
    }
    
    return UBASE_ERR_NONE;

upipe_src_udp_chrome_set_uri_err:
    free(string);
    return UBASE_ERR_INVALID;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_src_udp_chrome_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UREF_MGR:
            return upipe_src_udp_chrome_attach_uref_mgr(upipe);
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_src_udp_chrome_set_upump(upipe, NULL);
            return upipe_src_udp_chrome_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UBUF_MGR:
            return upipe_src_udp_chrome_attach_ubuf_mgr(upipe);
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_src_udp_chrome_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_src_udp_chrome_set_output(upipe, output);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_src_udp_chrome_set_uri(upipe, uri);
        }
        case UPIPE_ATTACH_UCLOCK:
             return upipe_src_udp_chrome_attach_uclock(upipe);
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_src_udp_chrome_get_flow_def(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_src_udp_chrome_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_src_udp_chrome_control(upipe, command, args));
    struct upipe_src_udp_chrome *upipe_src_udp_chrome = upipe_src_udp_chrome_from_upipe(upipe);

    if (upipe_src_udp_chrome->upump_mgr != NULL && upipe_src_udp_chrome->upump == NULL) {
        struct upump *upump = uqueue_upump_alloc_pop(&upipe_src_udp_chrome->bufferUDP, upipe_src_udp_chrome->upump_mgr, upipe_src_udp_chrome_idler, upipe);
        if (unlikely(!upump)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }

        upipe_src_udp_chrome_set_upump(upipe, upump);
        upump_start(upump);
    }
    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_src_udp_chrome_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_src_udp_chrome_clean_output(upipe);
    upipe_src_udp_chrome_clean_upump_mgr(upipe);
    upipe_src_udp_chrome_clean_upump(upipe);
    upipe_src_udp_chrome_clean_uclock(upipe);
    upipe_src_udp_chrome_clean_ubuf_mgr(upipe);
    upipe_src_udp_chrome_clean_uref_mgr(upipe);
    upipe_src_udp_chrome_clean_urefcount(upipe);
    upipe_src_udp_chrome_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_src_udp_chrome_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SRC_UDP_CHROME_SIGNATURE,
    .upipe_input = NULL,
    .upipe_alloc = upipe_src_udp_chrome_alloc,
    .upipe_control = upipe_src_udp_chrome_control,
    .upipe_mgr_control = NULL
};

/** @This returns the management structure for udp_src_chrome pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_src_udp_chrome_mgr_alloc(void)
{
    return &upipe_src_udp_chrome_mgr;
}

