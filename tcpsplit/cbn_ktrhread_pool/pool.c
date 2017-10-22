#include <linux/init.h>      // included for __init and __exit macros
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/printk.h>

static int pool_size = 16;		//Modify with debugfs
static struct kmem_cache *pool_slab;
static struct list_head kthread_pool;

static int (*pool_task)(void *data) = NULL;

static int pipe_loop_task(void *data)
{
	while (!kthread_should_stop()) {
		pr_err("%s running\n", current->comm);
		if (pool_task)
			pool_task(data);

		pr_err("%s sleep\n", current->comm);
		set_current_state(TASK_INTERRUPTIBLE);
		if (!kthread_should_stop())
			schedule();
		__set_current_state(TASK_RUNNING);
	}
	pr_err("%s end\n", current->comm);
	return 0;
}

static int (*threadfn)(void *data) = pipe_loop_task;

struct pool_elem {
	struct list_head list;
	struct task_struct *task;

	union {
		uint64_t _unspec[5];
	};
};

static void test(void)
{
	struct list_head *itr, *tmp;
	pr_err("test\n");

	list_for_each_safe(itr, tmp, &kthread_pool) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
//		list_del(itr);
//		kthread_stop(task->task);
//		kmem_cache_free(pool_slab, task);
//		kthread_run

		wake_up_process(task->task);
	}
}

static int __init cbn_kthread_pool_init(void)
{
	int i;
	pr_err("starting server_task\n");
	INIT_LIST_HEAD(&kthread_pool);
	pool_slab = kmem_cache_create("pool-thread-cache",
				      sizeof(struct pool_elem), 0, 0, NULL);

	for (i = 0; i < pool_size; i++) {
		struct pool_elem *elem = kmem_cache_alloc(pool_slab, GFP_KERNEL);
		struct task_struct *k
			= kthread_create(threadfn, elem->_unspec, "pool-thread-%d", i);
		if (unlikely(!k)) {
			pr_err("failed to create kthread %d\n", i);
			return -ENOMEM;
		}
		INIT_LIST_HEAD(&elem->list);
		elem->task = k;
		list_add(&elem->list, &kthread_pool);
	}

	test();
	return 0;
}

static void __exit cbn_kthread_pool_clean(void)
{
	struct list_head *itr, *tmp;
	pr_err("stopping server_task\n");

	list_for_each_safe(itr, tmp, &kthread_pool) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
		list_del(itr);
		kthread_stop(task->task);
		kmem_cache_free(pool_slab, task);
	}

}

module_init(cbn_kthread_pool_init);
module_exit(cbn_kthread_pool_clean);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markuze Alex");
MODULE_DESCRIPTION("CBN Kthread Pool");

