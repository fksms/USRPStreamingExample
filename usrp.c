#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include <uhd.h>

#include "array_blocking_queue_integer.h"
#include "usrp.h"

// ---------------------Status---------------------
extern sig_atomic_t running;
extern pthread_mutex_t mutex;
// ------------------------------------------------

// ---------------For USRP Streaming---------------
extern double freq;
extern double rate;
extern double gain;
extern char *device_args;
extern size_t channel;
extern char *antenna;
extern size_t num_samps_per_once;

extern stream_data_t *stream_buffer;
extern Array_Blocking_Queue_Integer abq1;
// ------------------------------------------------

int usrp_setup(uhd_usrp_handle *usrp)
{
    // UHD error codes
    uhd_error error;

    // Create USRP
    error = uhd_usrp_make(usrp, device_args);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    return 0;
}

int usrp_rx_setup(uhd_usrp_rx_handle *usrp_rx)
{
    // USRP handle
    uhd_usrp_handle usrp = usrp_rx->usrp;

    // UHD error codes
    uhd_error error;

    // Create other necessary structs
    uhd_tune_request_t tune_request = {
        .target_freq = freq,
        .rf_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
        .dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
    };
    uhd_tune_result_t tune_result;

    // Set RX antenna
    error = uhd_usrp_set_rx_antenna(usrp, antenna, channel);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Set rate
    error = uhd_usrp_set_rx_rate(usrp, rate, channel);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // See what rate actually is
    error = uhd_usrp_get_rx_rate(usrp, channel, &rate);
    printf("Actual RX Rate: %f Sps...\n", rate);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Set gain
    error = uhd_usrp_set_rx_gain(usrp, gain, channel, "");
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // See what rate actually is
    error = uhd_usrp_get_rx_gain(usrp, channel, "", &gain);
    printf("Actual RX Gain: %f dB...\n", gain);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Set frequency
    error = uhd_usrp_set_rx_freq(usrp, &tune_request, channel, &tune_result);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // See what rate actually is
    error = uhd_usrp_get_rx_freq(usrp, channel, &freq);
    printf("Actual RX frequency: %f Hz...\n", freq);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Specify complex<int16_t> as the CPU format.
    uhd_stream_args_t stream_args = {
        .cpu_format = "sc16",
        .otw_format = "sc16",
        .args = "",
        .channel_list = &channel,
        .n_channels = 1};

    // Define how device streams to host
    uhd_stream_cmd_t stream_cmd = {
        .stream_mode = UHD_STREAM_MODE_START_CONTINUOUS,
        .stream_now = 1,
    };

    // Create RX streamer
    error = uhd_rx_streamer_make(&usrp_rx->rx_streamer);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Create RX metadata
    error = uhd_rx_metadata_make(&usrp_rx->rx_metadata);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Set up streamer
    error = uhd_usrp_get_rx_stream(usrp, &stream_args, usrp_rx->rx_streamer);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Issue stream command
    error = uhd_rx_streamer_issue_stream_cmd(usrp_rx->rx_streamer, &stream_cmd);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Allocate a buffer for streaming
    //
    // ----------------------------------------
    // バッファ1要素あたりの確保するメモリ量
    // (sizeof(stream_data_t) + sizeof(int16_t) * num_samps_per_once * 2)
    //
    // sizeof(stream_data_t)：stream_data_t構造体のサイズ
    // sizeof(int16_t)      ：1サンプルあたりのデータサイズ（int16_t）（可変長配列メンバ用）
    // num_samps_per_once   ：1回で取得するサンプル数
    // 2                    ：I+Q
    // ----------------------------------------
    //
    size_t element_size = sizeof(stream_data_t) + sizeof(int16_t) * num_samps_per_once * 2;
    stream_buffer = (stream_data_t *)malloc(element_size * RX_STREAMER_RECV_QUEUE_SIZE);

    return 0;
}

void *usrp_stream_thread(void *arg)
{
    // USRP RX handle
    uhd_usrp_rx_handle *usrp_rx = arg;

    // Error condition on a receive call
    uhd_rx_metadata_error_code_t error_code;

    // 実際に取得したサンプル数
    size_t actual_num_samps;

    // バッファへのポインタ
    void *pointer_to_buf = NULL;

    // タイムアウト時間
    double timeout = 3.0; //[sec]

    // Array_Blocking_Queueの何番目に格納したかを示すインデックス
    int abq1_index = 0;

    // Actual streaming
    while (running)
    {
        // バッファへのポインタを設定
        pointer_to_buf = stream_buffer[abq1_index].samples;

        // ストリームを受信
        uhd_rx_streamer_recv(usrp_rx->rx_streamer, &pointer_to_buf, num_samps_per_once, &usrp_rx->rx_metadata, timeout, false, &actual_num_samps);
        uhd_rx_metadata_error_code(usrp_rx->rx_metadata, &error_code);

        // エラー有りの場合
        if (error_code)
        {
            // エラー有りの場合は解放して終了
            // printf("Streaming error: %d\n", error_code);
            // break;

            // エラー有りの場合はContinueする
            printf("Streaming error: %d\n", error_code);
            continue;
        }

        // 実際に取得したサンプルの数を格納
        stream_buffer[abq1_index].num_of_samples = actual_num_samps;

        // キューへの追加が失敗した場合は解放して終了
        if (blocking_queue_add(&abq1, abq1_index))
        {
            printf("Streame buffer is full.\n");
            break;
        }

        // インデックスをインクリメント
#if (RX_STREAMER_RECV_QUEUE_SIZE & (RX_STREAMER_RECV_QUEUE_SIZE - 1)) == 0 // Queue sizeが2の冪乗の場合
        abq1_index = (abq1_index + 1) & (RX_STREAMER_RECV_QUEUE_SIZE - 1);
#else
        abq1_index = (abq1_index + 1) % RX_STREAMER_RECV_QUEUE_SIZE;
#endif
    }

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    return NULL;
}

int usrp_rx_close(uhd_usrp_rx_handle *usrp_rx)
{
    // Memory release
    free(stream_buffer);

    // UHD error codes
    uhd_error error;

    // Define how device streams to host
    uhd_stream_cmd_t stream_cmd = {
        .stream_mode = UHD_STREAM_MODE_STOP_CONTINUOUS,
        .stream_now = 1,
    };

    // Issue stream command
    error = uhd_rx_streamer_issue_stream_cmd(usrp_rx->rx_streamer, &stream_cmd);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Cleaning up RX streamer
    error = uhd_rx_streamer_free(&usrp_rx->rx_streamer);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    // Cleaning up RX metadata
    error = uhd_rx_metadata_free(&usrp_rx->rx_metadata);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    return 0;
}

int usrp_close(uhd_usrp_handle usrp)
{
    // UHD error codes
    uhd_error error;

    // Cleaning up USRP
    error = uhd_usrp_free(&usrp);
    if (error)
    {
        printf("%u\n", error);
        return error;
    }

    return 0;
}