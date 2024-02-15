#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <uhd.h>

#include "structure.h"
#include "blocking_queue.h"


extern double freq;
extern double rate;
extern double gain;
extern char *device_args;
extern size_t channel;

extern int running;

extern Blocking_Queue samples_queue;


uhd_usrp_handle usrp_setup(void) {
    
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


void *usrp_stream_thread(void *arg) {

    // USRP handle
    uhd_usrp_handle usrp = arg;
    uhd_error error;

    size_t samps_per_buff, num_rx_samps;
    void *buf = NULL;
    uhd_rx_metadata_error_code_t error_code;    

    // CPUフォーマットは complex<int16_t> を指定
    uhd_stream_args_t stream_args = {
        .cpu_format = "sc16",
        .otw_format = "sc16",
        .args = "",
        .channel_list = &channel,
        .n_channels = 1
    };

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
    uhd_rx_metadata_handle md;
    error = uhd_rx_metadata_make(&md);
    if (error)
        printf("%u\n", error);

    // Set up streamer
    error = uhd_usrp_get_rx_stream(usrp, &stream_args, rx_streamer);
    if (error)
        printf("%u\n", error);

    // Set up buffer
    error = uhd_rx_streamer_max_num_samps(rx_streamer, &samps_per_buff);
    printf("Buffer size in samples: %zu\n", samps_per_buff);
    if (error)
        printf("%u\n", error);

    // Issue stream command
    error = uhd_rx_streamer_issue_stream_cmd(rx_streamer, &stream_cmd);
    if (error)
        printf("%u\n", error);

    // ----------------------------------------
    // バッファあたりの確保するメモリ量
    // (sizeof(size_t) + samps_per_buff * 2 * sizeof(int16_t))
    //
    // sizeof(size_t)   ：受信したサンプル数を格納する用
    // samps_per_buff   ：1回あたりに受信するサンプル数
    // 2                ：I+Q
    // sizeof(int16_t)  ：1サンプルあたりのサイズ（int16_t）
    // ----------------------------------------
    size_t buf_size = sizeof(size_t) + samps_per_buff * 2 * sizeof(int16_t);

    // Actual streaming
    while (running) {
        sample_buf_t *sb = malloc(buf_size);
        buf = sb->samples;
        uhd_rx_streamer_recv(rx_streamer, &buf, samps_per_buff, &md, 3.0, false, &num_rx_samps);
	    uhd_rx_metadata_error_code(md, &error_code);

        // エラー有りの場合は解放して終了
        if(error_code) {
            printf("Streaming Error: %d\n", error_code);
            free(sb);
            break;
        }
        sb->num_of_samples = num_rx_samps;

        // キューへの追加が失敗した場合は解放して終了
        if (blocking_queue_add(&samples_queue, sb)) {
            printf("Buffer is full.\n");
            free(sb);
            break;
        }
    }

    // Stop
    running = 0;

    // Issue stream command
    stream_cmd.stream_mode = UHD_STREAM_MODE_STOP_CONTINUOUS;
    error = uhd_rx_streamer_issue_stream_cmd(rx_streamer, &stream_cmd);
    if (error)
        printf("%u\n", error);

    // キュー内の要素（sample_buf_t）を全て取り出して解放
    // -> main側で実施

    // Cleaning up RX streamer
    error = uhd_rx_streamer_free(&rx_streamer);
    if (error)
        printf("%u\n", error);

    // Cleaning up RX metadata
    error = uhd_rx_metadata_free(&md);
    if (error)
        printf("%u\n", error);
    
    return NULL;
}


void usrp_close(uhd_usrp_handle usrp) {

    uhd_error error;

    // Cleaning up USRP
    error = uhd_usrp_free(&usrp);
    if (error)
        printf("%u\n", error);
}