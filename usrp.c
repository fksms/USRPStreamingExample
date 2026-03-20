#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <uhd.h>

#include "lfrb.h"
#include "usrp.h"

// ---------------------Status---------------------
extern _Atomic bool running;
// ------------------------------------------------

// --------------------For USRP--------------------
extern char *device_args;
// ------------------------------------------------

// ------------------For USRP RX-------------------
extern double rx_freq;
extern double rx_gain;
extern size_t rx_channel;
extern char *rx_antenna;
// ------------------------------------------------

// ------------------For USRP TX-------------------
double tx_freq = 919e6;
double tx_gain = 35.0;
size_t tx_channel = 1;
char *tx_antenna = "TX/RX";

double send_time = 0.2; //[sec]
// ------------------------------------------------

// ---------------------Buffer---------------------
extern LockFreeRingBuffer lfrb;
// ------------------------------------------------

int usrp_setup(uhd_usrp_handle *usrp) {
    // UHD error codes
    uhd_error error;

    // Create USRP
    error = uhd_usrp_make(usrp, device_args);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    return 0;
}

int usrp_rx_setup(uhd_usrp_rx_handle *usrp_rx) {
    // USRP handle
    uhd_usrp_handle usrp = usrp_rx->usrp;

    // UHD error codes
    uhd_error error;

    // Create other necessary structs
    uhd_tune_request_t tune_request = {
        .target_freq = rx_freq,
        .rf_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
        .dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
    };
    uhd_tune_result_t tune_result;

    // Set antenna
    error = uhd_usrp_set_rx_antenna(usrp, rx_antenna, rx_channel);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Set rate
    error = uhd_usrp_set_rx_rate(usrp, RX_SAMP_RATE, rx_channel);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // See what rate actually is
    double rx_rate;
    error = uhd_usrp_get_rx_rate(usrp, rx_channel, &rx_rate);
    printf("Actual RX rate: %f Sps...\n", rx_rate);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Set gain
    error = uhd_usrp_set_rx_gain(usrp, rx_gain, rx_channel, "");
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // See what gain actually is
    error = uhd_usrp_get_rx_gain(usrp, rx_channel, "", &rx_gain);
    printf("Actual RX gain: %f dB...\n", rx_gain);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Set frequency
    error = uhd_usrp_set_rx_freq(usrp, &tune_request, rx_channel, &tune_result);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // See what frequency actually is
    error = uhd_usrp_get_rx_freq(usrp, rx_channel, &rx_freq);
    printf("Actual RX frequency: %f Hz...\n", rx_freq);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Specify complex<int16_t> as the CPU format.
    uhd_stream_args_t stream_args = {.cpu_format = "sc16", .otw_format = "sc16", .args = "", .channel_list = &rx_channel, .n_channels = 1};

    // Create RX streamer
    error = uhd_rx_streamer_make(&usrp_rx->rx_streamer);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Create RX metadata
    error = uhd_rx_metadata_make(&usrp_rx->rx_metadata);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Set up streamer
    error = uhd_usrp_get_rx_stream(usrp, &stream_args, usrp_rx->rx_streamer);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    return 0;
}

int usrp_tx_setup(uhd_usrp_tx_handle *usrp_tx) {
    // USRP handle
    uhd_usrp_handle usrp = usrp_tx->usrp;

    // UHD error codes
    uhd_error error;

    // Create other necessary structs
    uhd_tune_request_t tune_request = {
        .target_freq = tx_freq,
        .rf_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
        .dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
    };
    uhd_tune_result_t tune_result;

    // Set antenna
    error = uhd_usrp_set_tx_antenna(usrp, tx_antenna, tx_channel);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Set rate
    error = uhd_usrp_set_tx_rate(usrp, TX_SAMP_RATE, tx_channel);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // See what rate actually is
    double tx_rate;
    error = uhd_usrp_get_tx_rate(usrp, tx_channel, &tx_rate);
    printf("Actual TX rate: %f Sps...\n", tx_rate);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Set gain
    error = uhd_usrp_set_tx_gain(usrp, tx_gain, tx_channel, "");
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // See what gain actually is
    error = uhd_usrp_get_tx_gain(usrp, tx_channel, "", &tx_gain);
    printf("Actual TX gain: %f dB...\n", tx_gain);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Set frequency
    error = uhd_usrp_set_tx_freq(usrp, &tune_request, tx_channel, &tune_result);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // See what frequency actually is
    error = uhd_usrp_get_tx_freq(usrp, tx_channel, &tx_freq);
    printf("Actual TX frequency: %f Hz...\n", tx_freq);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Specify complex<int16_t> as the CPU format.
    uhd_stream_args_t stream_args = {.cpu_format = "sc16", .otw_format = "sc16", .args = "", .channel_list = &tx_channel, .n_channels = 1};

    // Create TX streamer
    error = uhd_tx_streamer_make(&usrp_tx->tx_streamer);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Create TX metadata
    error = uhd_tx_metadata_make(&usrp_tx->tx_metadata, false, 0, 0.1, true, false);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Set up streamer
    error = uhd_usrp_get_tx_stream(usrp, &stream_args, usrp_tx->tx_streamer);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    return 0;
}

void *usrp_rx_thread(void *arg) {
    // USRP RX handle
    uhd_usrp_rx_handle *usrp_rx = arg;

    // 実際に取得したサンプル数
    size_t actual_num_samps;

    // タイムアウト時間
    double timeout = 3.0; //[sec]

    // ストリーミングデータを格納するためのバッファ
    static iq_sample_t recv_buf[RX_NUM_SAMPS * 2];

    // UHD は void* の配列を受け取る
    void *buf_ptrs[1] = {recv_buf};

    // Issue stream command to start streaming
    uhd_stream_cmd_t stream_cmd = {
        .stream_mode = UHD_STREAM_MODE_START_CONTINUOUS,
        .stream_now = 1,
    };
    uhd_rx_streamer_issue_stream_cmd(usrp_rx->rx_streamer, &stream_cmd);

    // Actual streaming
    while (atomic_load(&running)) {
        // ストリームを受信
        uhd_rx_streamer_recv(usrp_rx->rx_streamer, buf_ptrs, RX_NUM_SAMPS, &usrp_rx->rx_metadata, timeout, false, &actual_num_samps);

        // 受信サンプル数が想定と異なる場合
        if (actual_num_samps != RX_NUM_SAMPS) {
            // エラー有りの場合は解放して終了
            printf("Streaming error: actual_num_samps = %zu\n", actual_num_samps);
            break;

            // エラー有りの場合はContinueする
            // printf("Streaming error: actual_num_samps = %zu\n", actual_num_samps);
            // memset(recv_buf + actual_num_samps * 2, 0, (RX_NUM_SAMPS - actual_num_samps) * sizeof(iq_sample_t) * 2);
            // continue;
        }

        if (!lfrb_write(&lfrb, recv_buf, RX_NUM_SAMPS * 2)) {
            // バッファ溢れの場合
            printf("Ring buffer overflow.\n");
            break;
        }
    }

    // Issue stream command to stop streaming
    stream_cmd.stream_mode = UHD_STREAM_MODE_STOP_CONTINUOUS;
    uhd_rx_streamer_issue_stream_cmd(usrp_rx->rx_streamer, &stream_cmd);

    // Stop
    atomic_store(&running, false);

    return NULL;
}

void *usrp_tx_thread(void *arg) {
    // USRP TX handle
    uhd_usrp_tx_handle *usrp_tx = arg;

    // 実際に送信したサンプル数
    size_t actual_num_samps;

    // タイムアウト時間
    double timeout = 3.0; //[sec]

    // 送信する信号のバッファ
    static int16_t send_buf[TX_NUM_SAMPS * 2];

    // 送信する信号を生成（例：I成分が1、Q成分が1の単純な信号）
    for (size_t i = 0; i < TX_NUM_SAMPS; ++i) {
        send_buf[2 * i] = 1;     // I成分
        send_buf[2 * i + 1] = 1; // Q成分
    }

    // UHD は void* の配列を受け取る
    const void *buf_ptrs[1] = {send_buf};

    // 送信ループの回数を計算
    int loop = (int)((send_time * TX_SAMP_RATE) / TX_NUM_SAMPS);

    while (atomic_load(&running)) {
        for (int i = 0; i < loop; ++i) {
            // ストリームを送信
            uhd_tx_streamer_send(usrp_tx->tx_streamer, buf_ptrs, TX_NUM_SAMPS, &usrp_tx->tx_metadata, timeout, &actual_num_samps);
        }

        // 0.5秒スリープ
        usleep(500000);

        // center_freqを200kHz上げる
        tx_freq += 200e3;

        // Set frequency
        uhd_tune_request_t tune_request = {
            .target_freq = tx_freq,
            .rf_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
            .dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
        };
        uhd_tune_result_t tune_result;

        // Set frequency
        uhd_error error = uhd_usrp_set_tx_freq(usrp_tx->usrp, &tune_request, tx_channel, &tune_result);
        if (error) {
            printf("TX freq set error: %u\n", error);
            break;
        }

        printf("TX freq changed: %f Hz\n", tx_freq);
    }

    // Stop
    atomic_store(&running, false);

    return NULL;
}

int usrp_rx_close(uhd_usrp_rx_handle *usrp_rx) {
    // UHD error codes
    uhd_error error;

    // Cleaning up RX streamer
    error = uhd_rx_streamer_free(&usrp_rx->rx_streamer);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Cleaning up RX metadata
    error = uhd_rx_metadata_free(&usrp_rx->rx_metadata);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    return 0;
}

int usrp_tx_close(uhd_usrp_tx_handle *usrp_tx) {
    // UHD error codes
    uhd_error error;

    // Cleaning up TX streamer
    error = uhd_tx_streamer_free(&usrp_tx->tx_streamer);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    // Cleaning up TX metadata
    error = uhd_tx_metadata_free(&usrp_tx->tx_metadata);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    return 0;
}

int usrp_close(uhd_usrp_handle *usrp) {
    // UHD error codes
    uhd_error error;

    // Cleaning up USRP
    error = uhd_usrp_free(usrp);
    if (error) {
        printf("%u\n", error);
        return error;
    }

    return 0;
}