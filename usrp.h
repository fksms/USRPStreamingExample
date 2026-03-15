#ifndef __USRP_H__
#define __USRP_H__

#include <uhd.h>
#include <stdio.h>
#include <stdint.h>

// USRP RX handle
typedef struct
{
    // USRP handle
    uhd_usrp_handle usrp;
    // RX streamer handle
    uhd_rx_streamer_handle rx_streamer;
    // RX metadata handle
    uhd_rx_metadata_handle rx_metadata;
} uhd_usrp_rx_handle;

int usrp_setup(uhd_usrp_handle *usrp);
int usrp_rx_setup(uhd_usrp_rx_handle *usrp_rx);
void *usrp_stream_thread(void *arg);
int usrp_rx_close(uhd_usrp_rx_handle *usrp_rx);
int usrp_close(uhd_usrp_handle usrp);

#endif // __USRP_H__