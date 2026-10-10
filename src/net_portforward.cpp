/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/**
 * @file net_portforward.cpp
 *     Port forwarding support using NAT-PMP and UPnP.
 * @par Purpose:
 *     Tries NAT-PMP first (simpler/faster), falls back to UPnP.
 * @author   KeeperFX Team
 * @date     01 Jan 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "net_portforward.h"
#include "bflib_basics.h"
#include "bflib_datetm.h"

#define MINIUPNP_STATICLIB
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

#define NATPMP_STATICLIB
#include <natpmp.h>

#include <cstdio>
#include <ctime>
#include <thread>

#define NATPMP_TIMEOUT_SECONDS 1.0
#define UPNP_TIMEOUT_MS 3000

#include "post_inc.h"

enum PortForwardMethod {
    PORT_FORWARD_NONE = 0,
    PORT_FORWARD_NATPMP,
    PORT_FORWARD_UPNP
};

static enum PortForwardMethod active_method = PORT_FORWARD_NONE;
static uint16_t mapped_port = 0;
static std::thread mapping_thread;

static struct UPNPUrls upnp_urls;
static struct IGDdatas upnp_data;
static char upnp_lanaddr[64];

static natpmp_t natpmp;

static int is_cgnat_detected() {
    return 0;
}

static int natpmp_add_port_mapping(uint16_t port) {
    clock_t start_time = LbTimerClock();
    if (initnatpmp(&natpmp, 0, 0) < 0) {
        LbNetLog("NAT-PMP: Failed to initialize\n");
        return 0;
    }
    if (sendpublicaddressrequest(&natpmp) < 0) {
        LbNetLog("NAT-PMP: Failed to send public address request\n");
        closenatpmp(&natpmp);
        return 0;
    }
    natpmpresp_t response;
    fd_set file_descriptors;
    struct timeval timeout;
    int result;
    do {
        double elapsed = (double)(LbTimerClock() - start_time) / 1000.0;
        if (elapsed > NATPMP_TIMEOUT_SECONDS) {
            LbNetLog("NAT-PMP: Timeout getting public address\n");
            closenatpmp(&natpmp);
            return 0;
        }
        FD_ZERO(&file_descriptors);
        FD_SET(natpmp.s, &file_descriptors);
        getnatpmprequesttimeout(&natpmp, &timeout);
        select(FD_SETSIZE, &file_descriptors, NULL, NULL, &timeout);
        result = readnatpmpresponseorretry(&natpmp, &response);
    } while (result == NATPMP_TRYAGAIN);
    if (result < 0) {
        LbNetLog("NAT-PMP: Failed to get public address\n");
        closenatpmp(&natpmp);
        return 0;
    }
    if (sendnewportmappingrequest(&natpmp, NATPMP_PROTOCOL_UDP, port, port, 0x7FFFFFFF) < 0) {
        LbNetLog("NAT-PMP: Failed to send port mapping request\n");
        closenatpmp(&natpmp);
        return 0;
    }
    do {
        double elapsed = (double)(LbTimerClock() - start_time) / 1000.0;
        if (elapsed > NATPMP_TIMEOUT_SECONDS) {
            LbNetLog("NAT-PMP: Timeout on port mapping\n");
            closenatpmp(&natpmp);
            return 0;
        }
        FD_ZERO(&file_descriptors);
        FD_SET(natpmp.s, &file_descriptors);
        getnatpmprequesttimeout(&natpmp, &timeout);
        select(FD_SETSIZE, &file_descriptors, NULL, NULL, &timeout);
        result = readnatpmpresponseorretry(&natpmp, &response);
    } while (result == NATPMP_TRYAGAIN);
    if (result < 0) {
        LbNetLog("NAT-PMP: Permanent lease rejected, trying timed lease\n");
        if (sendnewportmappingrequest(&natpmp, NATPMP_PROTOCOL_UDP, port, port, 3600) < 0) {
            LbNetLog("NAT-PMP: Failed to send timed port mapping request\n");
            closenatpmp(&natpmp);
            return 0;
        }
        do {
            double elapsed = (double)(LbTimerClock() - start_time) / 1000.0;
            if (elapsed > NATPMP_TIMEOUT_SECONDS) {
                LbNetLog("NAT-PMP: Timeout on timed lease\n");
                closenatpmp(&natpmp);
                return 0;
            }
            FD_ZERO(&file_descriptors);
            FD_SET(natpmp.s, &file_descriptors);
            getnatpmprequesttimeout(&natpmp, &timeout);
            select(FD_SETSIZE, &file_descriptors, NULL, NULL, &timeout);
            result = readnatpmpresponseorretry(&natpmp, &response);
        } while (result == NATPMP_TRYAGAIN);
        if (result < 0) {
            LbNetLog("NAT-PMP: Failed to add port mapping\n");
            closenatpmp(&natpmp);
            return 0;
        }
    }
    LbNetLog("NAT-PMP: port forwarding active on port %u\n", port);
    mapped_port = port;
    active_method = PORT_FORWARD_NATPMP;
    closenatpmp(&natpmp);
    return 1;
}

static void port_forward_add_mapping_internal(uint16_t port)
{
    if (is_cgnat_detected()) {
        LbNetLog("CGNAT detected, automatic port forwarding unavailable\n");
        return;
    }
    if (natpmp_add_port_mapping(port)) {
        return;
    }
    int error = 0;
    struct UPNPDev *device_list = upnpDiscover(UPNP_TIMEOUT_MS, NULL, NULL, 0, 0, 2, &error);
    if (!device_list) {
        LbNetLog("UPnP: no devices found, port forwarding unavailable, UDP hole punching will be used\n");
        return;
    }
#if (MINIUPNPC_API_VERSION >= 18)
    int internet_gateway_device_result = UPNP_GetValidIGD(device_list, &upnp_urls, &upnp_data, upnp_lanaddr, sizeof(upnp_lanaddr), NULL, 0);
#else
    int internet_gateway_device_result = UPNP_GetValidIGD(device_list, &upnp_urls, &upnp_data, upnp_lanaddr, sizeof(upnp_lanaddr));
#endif
    freeUPNPDevlist(device_list);
    if (internet_gateway_device_result == 0) {
        LbNetLog("UPnP: router found but no valid IGD, port forwarding unavailable, UDP hole punching will be used\n");
        FreeUPNPUrls(&upnp_urls);
        return;
    }
    char port_string[16];
    snprintf(port_string, sizeof(port_string), "%u", port);
    UPNP_DeletePortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, "UDP", "");
    LbNetLog("UPnP: lanaddr=%s\n", upnp_lanaddr);
    int result = UPNP_AddPortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, port_string, upnp_lanaddr, "KeeperFX", "UDP", "", "0");
    if (result != UPNPCOMMAND_SUCCESS) {
        LbNetLog("UPnP: permanent lease rejected (error %d), trying timed lease\n", result);
        result = UPNP_AddPortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, port_string, upnp_lanaddr, "KeeperFX", "UDP", "", "3600");
        if (result != UPNPCOMMAND_SUCCESS) {
            LbNetLog("UPnP: failed to add port mapping (error %d), UDP hole punching will be used\n", result);
            FreeUPNPUrls(&upnp_urls);
            return;
        }
    }
    LbNetLog("UPnP: port forwarding active on port %u\n", port);
    mapped_port = port;
    active_method = PORT_FORWARD_UPNP;
}

int port_forward_add_mapping(uint16_t port)
{
    port_forward_remove_mapping();
    mapping_thread = std::thread(port_forward_add_mapping_internal, port);
    return 1;
}

void port_forward_remove_mapping(void)
{
    if (mapping_thread.joinable()) {
        mapping_thread.join();
    }
    if (active_method == PORT_FORWARD_NONE || mapped_port == 0) {
        return;
    }
    if (active_method == PORT_FORWARD_NATPMP) {
        if (initnatpmp(&natpmp, 0, 0) < 0) {
            LbNetLog("NAT-PMP: Failed to initialize for removal\n");
            mapped_port = 0;
            active_method = PORT_FORWARD_NONE;
            return;
        }
        sendnewportmappingrequest(&natpmp, NATPMP_PROTOCOL_UDP, mapped_port, 0, 0);
        natpmpresp_t response;
        fd_set file_descriptors;
        struct timeval timeout;
        int result;
        clock_t start_time = LbTimerClock();
        do {
            double elapsed = (double)(LbTimerClock() - start_time) / 1000.0;
            if (elapsed > NATPMP_TIMEOUT_SECONDS) {
                break;
            }
            FD_ZERO(&file_descriptors);
            FD_SET(natpmp.s, &file_descriptors);
            getnatpmprequesttimeout(&natpmp, &timeout);
            select(FD_SETSIZE, &file_descriptors, NULL, NULL, &timeout);
            result = readnatpmpresponseorretry(&natpmp, &response);
        } while (result == NATPMP_TRYAGAIN);
        closenatpmp(&natpmp);
    } else if (active_method == PORT_FORWARD_UPNP) {
        char port_string[16];
        snprintf(port_string, sizeof(port_string), "%u", mapped_port);
        UPNP_DeletePortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, "UDP", NULL);
        FreeUPNPUrls(&upnp_urls);
    }
    mapped_port = 0;
    active_method = PORT_FORWARD_NONE;
}
