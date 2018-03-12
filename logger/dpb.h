#ifndef __TRVL_LOG__
#define __TRVL_LOG__

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/slab.h>

#define TRVL_BUFFER_SIZE	(PAGE_SIZE - 128)
#define BUFF_SIZE(x) TRVL_BUFFER_SIZE

struct trvl_buffer_mgr {
	struct list_head list;
	int len;
	char buffer[TRVL_BUFFER_SIZE];
};

static inline int trvlb_init(struct trvl_buffer_mgr *mgr)
{
	INIT_LIST_HEAD(&mgr->list);
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
	if (len > BUFF_SIZE(mgr))
		return 0;
	if ((len + mgr->len) > BUFF_SIZE(mgr))
		mgr->len = 0;
	memcpy(&mgr->buffer[mgr->len], buff, len);
	mgr->len += len;
	return len;
}

static inline char *trvlb_pull_formated_buffer(struct trvl_buffer_mgr *mgr, int *size)
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

static inline char* trvlb_get_buff_head(struct trvl_buffer_mgr *mgr, int *tailroom)
{
	*tailroom = BUFF_SIZE(mgr) - mgr->len;
	return &mgr->buffer[mgr->len];
}

static inline int trvlb_put_buff_head(struct trvl_buffer_mgr *mgr, int len)
{
	if ((mgr->len + len) > BUFF_SIZE(mgr))
		return -EINVAL;
	mgr->len += len;
	return 0;
}

struct dp_logger {
	struct list_head list;
	int cnt;
};

static inline int dp_logger_init(struct dp_logger *logger)
{
	INIT_LIST_HEAD(&logger->list);
	logger->cnt = 0;
	return 0;
}

static inline char *dp_logger_next_head(struct dp_logger *logger, int *tailroom)
{
	struct trvl_buffer_mgr *mgr = kmalloc(sizeof(struct trvl_buffer_mgr), GFP_ATOMIC);
	if (unlikely(!mgr)) {
		WARN_ONCE(!mgr, "dp_logger failed to kmalloc");
		return NULL;
	}
	trvlb_init(mgr);
	list_add_tail(&mgr->list, &logger->list);
	logger->cnt++;
	return trvlb_get_buff_head(mgr, tailroom);
}

static inline char *dp_logger_get_head(struct dp_logger *logger, int *tailroom)
{
	struct trvl_buffer_mgr *mgr = NULL;
	if (unlikely(!logger->cnt))
		return NULL;
	mgr = list_last_entry(&logger->list, struct trvl_buffer_mgr, list);
	return trvlb_get_buff_head(mgr, tailroom);
}

static inline int dp_logger_put_buff_head(struct dp_logger *logger, int len)
{
	struct trvl_buffer_mgr *mgr = NULL;
	if (unlikely(!logger->cnt))
		return -EINVAL;
	mgr = list_last_entry(&logger->list, struct trvl_buffer_mgr, list);
	return trvlb_put_buff_head(mgr, len);
}
/*
 * TODO:
	consume/pull_buffer
	mc support - per core list of trvlb
	pool of buffers...
*/

#define log_string(logger, fmt, ...) {								\
	int tailroom;										\
	int watchdog = 0;									\
	char *buff = dp_logger_get_head(logger, &tailroom); 					\
	while (buff) {										\
		int size  = scnprintf(buff, tailroom, "%s:"fmt, __FUNCTION__, ##__VA_ARGS__);	\
		buff = NULL;									\
		if (unlikely(size >= tailroom)) {						\
			if (likely(!watchdog++)) /*Prevent endless loop in weird corner cases*/	\
				buff = dp_logger_next_head(logger, &tailroom);			\
		} else {									\
			dp_logger_put_head(logger, size);					\
		}										\
	}											\
}

#endif /*__TRVL_LOG__*/
