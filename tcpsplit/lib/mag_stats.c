#include "linux/kernel.h"
#include "linux/debugfs.h"
#include "linux/slab.h"
#include "linux/mm.h"
#include <linux/dma-cache.h>

static struct dentry *dir;
static int  users;
static struct dev_iova_mag* entries[8];
static void dump_stats(void);

static int ta_dump_show(struct seq_file *m, void *v)
{
        dump_stats();
        return 0;
}

static int ta_dump_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, ta_dump_show, NULL);
}

static const struct file_operations ta_dump_fops = {
	.owner		= THIS_MODULE,
	.open		= ta_dump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int pcop_debug_init(void)
{
	dir = debugfs_create_dir("pcop_debug", NULL);
	if (unlikely(!dir))
		return PTR_ERR(dir);

	debugfs_create_file("get_stats", 0666, dir, NULL /* no specific val for this file */,
					&ta_dump_fops);
	return 0;
}

void mag_stats_register(void *reg)
{
	int idx = users;
	++users;
	if (users == 1)
		pcop_debug_init();

	entries[idx] = reg;
}

static inline void dump_iova_mag(struct dev_iova_mag* entry)
{
	int i = 0;
	struct mag_allocator *allocator;

	for (i = 0; i < 4; i++) {
		allocator = &entry->allocator[i];
		trace_printk("idx %d full count %d empty count %d\n",
				i, allocator->full_count, allocator->empty_count);
	}
}

static void dump_stats()
{
	int i = 0;
	for (i = 0; i < users; i++)
		dump_iova_mag(entries[i]);
}
