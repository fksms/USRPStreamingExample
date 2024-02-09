#ifndef __USRP_H__
#define __USRP_H__

#include <uhd.h>

uhd_usrp_handle usrp_setup(void);
void *usrp_stream_thread(void *arg);
void usrp_close(uhd_usrp_handle usrp);

#endif