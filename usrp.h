#ifndef __USRP_H__
#define __USRP_H__

#include <stdint.h>
#include <stdio.h>
#include <uhd.h>

// USRP RX handle
typedef struct {
    // USRP handle
    uhd_usrp_handle usrp;
    // RX streamer handle
    uhd_rx_streamer_handle rx_streamer;
    // RX metadata handle
    uhd_rx_metadata_handle rx_metadata;
} uhd_usrp_rx_handle;

// USRP TX handle
typedef struct {
    // USRP handle
    uhd_usrp_handle usrp;
    // TX streamer handle
    uhd_tx_streamer_handle tx_streamer;
    // TX metadata handle
    uhd_tx_metadata_handle tx_metadata;
} uhd_usrp_tx_handle;

int usrp_setup(uhd_usrp_handle *usrp);
int usrp_rx_setup(uhd_usrp_rx_handle *usrp_rx);
int usrp_tx_setup(uhd_usrp_tx_handle *usrp_tx);
void *usrp_rx_thread(void *arg);
void *usrp_tx_thread(void *arg);
int usrp_rx_close(uhd_usrp_rx_handle *usrp_rx);
int usrp_tx_close(uhd_usrp_tx_handle *usrp_tx);
int usrp_close(uhd_usrp_handle *usrp);

#endif // __USRP_H__