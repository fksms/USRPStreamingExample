#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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

extern stream_buf_t *stream_buffer;
extern Array_Blocking_Queue_Integer abq1;
// ------------------------------------------------

uhd_usrp_handle usrp_setup(void)
{
    // USRP handle
    uhd_usrp_handle usrp;
    uhd_error error;

    // Create other necessary structs
    uhd_tune_request_t tune_request = {
        .target_freq = freq,
        .rf_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
        .dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
    };
    uhd_tune_result_t tune_result;

    // Create USRP
    error = uhd_usrp_make(&usrp, device_args);
    if (error)
        printf("%u\n", error);

    // Set rate
    error = uhd_usrp_set_rx_rate(usrp, rate, channel);
    if (error)
        printf("%u\n", error);

    // See what rate actually is
    error = uhd_usrp_get_rx_rate(usrp, channel, &rate);
    printf("Actual RX Rate: %f Sps...\n", rate);
    if (error)
        printf("%u\n", error);

    // Set gain
    error = uhd_usrp_set_rx_gain(usrp, gain, channel, "");
    if (error)
        printf("%u\n", error);

    // See what rate actually is
    error = uhd_usrp_get_rx_gain(usrp, channel, "", &gain);
    printf("Actual RX Gain: %f dB...\n", gain);
    if (error)
        printf("%u\n", error);

    // Set frequency
    error = uhd_usrp_set_rx_freq(usrp, &tune_request, channel, &tune_result);
    if (error)
        printf("%u\n", error);

    // See what rate actually is
    error = uhd_usrp_get_rx_freq(usrp, channel, &freq);
    printf("Actual RX frequency: %f Hz...\n", freq);
    if (error)
        printf("%u\n", error);

    return usrp;
}

void *usrp_stream_thread(void *arg)
{
    // USRP handle
    uhd_usrp_handle usrp = arg;
    uhd_error error;

    // 取得したいサンプル数と実際に取得したサンプル数
    size_t num_samps, actual_num_samps;
    // バッファへのポインタ
    void *pointer_to_buf = NULL;
    // エラーコード
    uhd_rx_metadata_error_code_t error_code;

    // CPUフォーマットは complex<int16_t> を指定
    uhd_stream_args_t stream_args = {
        .cpu_format = "sc16",
        .otw_format = "sc16",
        .args = "",
        .channel_list = &channel,
        .n_channels = 1};

    uhd_stream_cmd_t stream_cmd = {
        .stream_mode = UHD_STREAM_MODE_START_CONTINUOUS,
        .stream_now = 1,
    };

    // Create RX streamer
    uhd_rx_streamer_handle rx_streamer;
    error = uhd_rx_streamer_make(&rx_streamer);
    if (error)
        printf("%u\n", error);

    // Create RX metadata
    uhd_rx_metadata_handle rx_metadata;
    error = uhd_rx_metadata_make(&rx_metadata);
    if (error)
        printf("%u\n", error);

    // Set up streamer
    error = uhd_usrp_get_rx_stream(usrp, &stream_args, rx_streamer);
    if (error)
        printf("%u\n", error);

    /*
    // Set up buffer
    error = uhd_rx_streamer_max_num_samps(rx_streamer, &num_samps);
    if (error)
        printf("%u\n", error);
    */

    // 1回で取得するサンプル数（最大は2040？）
    num_samps = 1024;
    printf("Number of samples taken at one time: %zu\n", num_samps);

    // Issue stream command
    error = uhd_rx_streamer_issue_stream_cmd(rx_streamer, &stream_cmd);
    if (error)
        printf("%u\n", error);

    // ----------------------------------------
    // バッファ1要素あたりの確保するメモリ量
    // (sizeof(size_t) + num_samps * 2 * sizeof(int16_t))
    //
    // sizeof(size_t)   ：実際に受信したサンプルの数を格納する用（size_t）
    // num_samps        ：1回で取得するサンプル数
    // 2                ：I+Q
    // sizeof(int16_t)  ：1サンプルあたりのサイズ（int16_t）
    // ----------------------------------------
    size_t element_size = sizeof(size_t) + num_samps * 2 * sizeof(int16_t);
    stream_buffer = (stream_buf_t *)malloc(element_size * RX_STREAMER_RECV_QUEUE_SIZE);

    // タイムアウト時間
    double timeout = 3.0; //[sec]

    // 配列のインデックス
    int array_index = 0;

    // Actual streaming
    while (running)
    {
        // バッファへのポインタを設定
        pointer_to_buf = stream_buffer[array_index].samples;

        // ストリームを受信
        uhd_rx_streamer_recv(rx_streamer, &pointer_to_buf, num_samps, &rx_metadata, timeout, false, &actual_num_samps);
        uhd_rx_metadata_error_code(rx_metadata, &error_code);

        // エラー有りの場合は解放して終了
        if (error_code)
        {
            printf("Streaming Error: %d\n", error_code);
            break;
        }

        // 実際に取得したサンプルの数を格納
        stream_buffer[array_index].num_of_samples = actual_num_samps;

        // キューへの追加が失敗した場合は解放して終了
        if (blocking_queue_add(&abq1, array_index))
        {
            printf("Buffer is full.\n");
            break;
        }

        // インデックスをインクリメント
#if (RX_STREAMER_RECV_QUEUE_SIZE & (RX_STREAMER_RECV_QUEUE_SIZE - 1)) == 0 // Queue sizeが2の冪乗の場合
        array_index = (array_index + 1) & (RX_STREAMER_RECV_QUEUE_SIZE - 1);
#else
        array_index = (array_index + 1) % RX_STREAMER_RECV_QUEUE_SIZE;
#endif
    }

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    // Issue stream command
    stream_cmd.stream_mode = UHD_STREAM_MODE_STOP_CONTINUOUS;
    error = uhd_rx_streamer_issue_stream_cmd(rx_streamer, &stream_cmd);
    if (error)
        printf("%u\n", error);

    // Cleaning up RX streamer
    error = uhd_rx_streamer_free(&rx_streamer);
    if (error)
        printf("%u\n", error);

    // Cleaning up RX metadata
    error = uhd_rx_metadata_free(&rx_metadata);
    if (error)
        printf("%u\n", error);

    return NULL;
}

void usrp_close(uhd_usrp_handle usrp)
{
    uhd_error error;

    // Cleaning up USRP
    error = uhd_usrp_free(&usrp);
    if (error)
        printf("%u\n", error);
}