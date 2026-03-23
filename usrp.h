#ifndef __USRP_H__
#define __USRP_H__

#include <stdint.h>
#include <stdio.h>
#include <uhd.h>

/* ---------------------------------------------------------------
 * RX_SAMP_RATE: 受信時のサンプリングレート [Hz]
 * RX_NUM_SAMPS: 1回の受信で取得するサンプル数
 * TX_SAMP_RATE: 送信時のサンプリングレート [Hz]
 * TX_NUM_SAMPS: 1回の送信で送るサンプル数
 * ---------------------------------------------------------------*/
#define RX_SAMP_RATE 10e6
#define RX_NUM_SAMPS 1000
#define TX_SAMP_RATE 200e3
#define TX_NUM_SAMPS 1000

// USRP RX handle
typedef struct {
    // Center frequency
    double rx_freq;
    // Gain
    double rx_gain;
    // Sampling rate
    double rx_samp_rate;
    // Channel (0 or 1)
    size_t rx_channel;
    // Antenna ("TX/RX" or "RX2")
    char *rx_antenna;
    // USRP handle
    uhd_usrp_handle usrp;
    // RX streamer handle
    uhd_rx_streamer_handle rx_streamer;
    // RX metadata handle
    uhd_rx_metadata_handle rx_metadata;
} uhd_usrp_rx_handle;

// USRP TX handle
typedef struct {
    // Center frequency
    double tx_freq;
    // Gain
    double tx_gain;
    // Sampling rate
    double tx_samp_rate;
    // Channel (0 or 1)
    size_t tx_channel;
    // Antenna ("TX/RX" or "RX2")
    char *tx_antenna;
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
