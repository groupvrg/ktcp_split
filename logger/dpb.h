#ifndef __DATA_PATH_LOG__
#define __DATA_PATH_LOG__

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/types.h>

#define BUFF_SIZE PAGE_SIZE

struct dp_buffer_mgr {
	char buffer[BUFF_SIZE];
	int len;
};

static inline int dpb_init(struct dp_buffer_mgr *mgr, uint bufsize)
{
	mgr->len = 0;
	return 0;
}

static inline int dpb_close(struct dp_buffer_mgr *mgr)
{
	mgr->len = 0;
	return 0;
}

static inline int dpb_log_formated_string(struct dp_buffer_mgr *mgr, char *buff, int len)
{
	if (len > BUFF_SIZE)
		return 0;
	if ((len + mgr->len) > BUFF_SIZE)
		mgr->len = 0;
	memcpy(&mgr->buffer[mgr->len], buff, len);
	mgr->len += len;
	return len;
}

static inline char*dpb_pull_formated_buffer(struct dp_buffer_mgr *mgr, int *size)
{
	*size = mgr->len;
	return mgr->buffer;
}

static inline void dpb_put_formated_buffer(struct dp_buffer_mgr *mgr, char *buffer, int flag)
{
	if (flag)
		mgr->len = 0;
	return;
}

#endif /*__DATA_PATH_LOG__*/
