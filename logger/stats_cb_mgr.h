#ifndef __STATS_CBM__
#define __STATS_CBM__

#include <linux/list.h>
#include "dpb.h"

struct stats_cb_entry {
	struct list_head list;
	void *ctx;
	int (*get_stat) (void *ctx, char *, int tailroom);
};

struct stats_cb_mgr {
	struct list_head list;
};

static inline int scbm_init(struct stats_cb_mgr *scbm)
{
	INIT_LIST_HEAD(&scbm->list);
	return 0;
}

static inline void scbm_close(struct stats_cb_mgr *scbm)
{
	return;
}

static inline int scbm_register(struct stats_cb_mgr *scbm,
				struct stats_cb_entry *entry)
{
	list_add_tail(&entry->list, &scbm->list);
	return 0;
}

static inline void scbm_collect_stats(struct stats_cb_mgr *scbm,
					struct trvl_buffer_mgr *mgr)
{
	struct list_head *itr, *tmp;

	list_for_each_safe(itr, tmp, &scbm->list) {
		char *buff;
		int size;
		struct stats_cb_entry *entry = list_entry(itr,
						struct stats_cb_entry, list);
		buff = trvlb_get_buff_head(mgr, &size);
		if ((size = entry->get_stat(entry->ctx, buff, size)) < 0)
			pr_err("CB is exeeding remainig trivial buffer len\n");
		trvlb_put_buff_head(mgr, size);
	}
}
#endif /*__STATS_CBM__*/
