#ifndef __TRVL_LOG__
#define __TRVL_LOG__

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/types.h>

#define BUFF_SIZE PAGE_SIZE

struct trvl_buffer_mgr {
	char buffer[BUFF_SIZE];
	int len;
};

static inline int trvlb_init(struct trvl_buffer_mgr *mgr, uint bufsize)
{
	mgr->len = 0;
	return 0;
}

static inline int trvlb_close(struct trvl_buffer_mgr *mgr)
{
	mgr->len = 0;
	return 0;
}

static inline int trvlb_log_formated_string(struct trvl_buffer_mgr *mgr, char *buff, int len)
{
	if (len > BUFF_SIZE)
		return 0;
	if ((len + mgr->len) > BUFF_SIZE)
		mgr->len = 0;
	memcpy(&mgr->buffer[mgr->len], buff, len);
	mgr->len += len;
	return len;
}

static inline char*trvlb_pull_formated_buffer(struct trvl_buffer_mgr *mgr, int *size)
{
	*size = mgr->len;
	return mgr->buffer;
}

static inline void trvlb_put_formated_buffer(struct trvl_buffer_mgr *mgr, char *buffer, int flag)
{
	if (flag)
		mgr->len = 0;
	return;
}

#endif /*__TRVL_LOG__*/
