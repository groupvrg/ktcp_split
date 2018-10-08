#include <linux/init.h>      // included for __init and __exit macros
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include "cbn_common.h"
#include "pool.h"

#define POOL_PRINT(...)
//#define POOL_PRINT TRACE_PRINT
#define POOL_ERR TRACE_PRINT

#define cbn_list_del(x) {POOL_PRINT("list_del(%d:%s): %p {%p, %p}", __LINE__, current->comm, x, (x)->next, (x)->prev); list_del((x));}
#define cbn_list_add(x, h) {POOL_PRINT("list_add(%d:%s): %p {%p, %p} h %p {%p, %p}", 	\
					__LINE__, current->comm,			\
					x, (x)->next, (x)->prev,			\
					h, (h)->next, (h)->prev);			\
					list_add((x), (h));}

static void kthread_pool_reuse(struct kthread_pool *cbn_pool, struct pool_elem *elem)
{
	spin_lock_irq(&cbn_pool->running_lock);
	list_del(&elem->list);
	spin_unlock_irq(&cbn_pool->running_lock);
	spin_lock_irq(&cbn_pool->pool_lock);
	list_add(&elem->list, &cbn_pool->kthread_pool);
	--cbn_pool->refil_needed;
	spin_unlock_irq(&cbn_pool->pool_lock);
}

static int pipe_loop_task(void *data)
{
	struct pool_elem *elem = data;
	struct kthread_pool *pool = elem->pool;

	while (!kthread_should_stop()) {
		POOL_PRINT("running %s", current->comm);
		if (elem->pool_task)
			elem->pool_task(elem->data);
		else
			POOL_ERR("ERROR %s: no pool task", __FUNCTION__);

		POOL_PRINT("sleeping %s", current->comm);
		set_current_state(TASK_INTERRUPTIBLE);
		if (!kthread_should_stop()) {
			POOL_PRINT("%s out to reuse <%p>", current->comm, current);
			elem->pool_task = NULL;
			kthread_pool_reuse(pool, elem);
			schedule();
		}
		__set_current_state(TASK_RUNNING);
	}
	return 0;
}

static int (*threadfn)(void *data) = pipe_loop_task;

static inline void refill_pool(struct kthread_pool *cbn_pool, int count)
{
	count = (count) ? count : cbn_pool->refil_needed;

	POOL_PRINT("pool %p count %d", cbn_pool, count);
	count = (count < 0) ? 0 : count;
	while (count--) {
		struct task_struct *k;
		struct pool_elem *elem = kmem_cache_alloc(cbn_pool->pool_slab, GFP_ATOMIC);
		if (unlikely(!elem)) {
			POOL_ERR("ERROR: elem is NULL");
			pr_err("ERROR: elem is NULL\n");
			return;
		}

		k = kthread_create(threadfn, elem, "pool-th-%d", cbn_pool->top_count);

		if (unlikely(!k)) {
			POOL_ERR("ERROR: failed to create kthread %d", cbn_pool->top_count);
			pr_err("ERROR: failed to create kthread %d\n", cbn_pool->top_count);
			kmem_cache_free(cbn_pool->pool_slab, elem);
			return;
		}
		INIT_LIST_HEAD(&elem->list);
		elem->task = k;
		elem->pool = cbn_pool;
		elem->pool_task = NULL;
		spin_lock_irq(&cbn_pool->pool_lock);
		list_add(&elem->list, &cbn_pool->kthread_pool);
		--cbn_pool->refil_needed;
		++cbn_pool->top_count;
		spin_unlock_irq(&cbn_pool->pool_lock);
		POOL_PRINT("pool thread %d [%p] allocated %llx", cbn_pool->top_count, elem, rdtsc());
	}
}

static int refil_thread(void *data)
{
	struct kthread_pool *cbn_pool = data;

	while (!kthread_should_stop()) {
		refill_pool(cbn_pool, 0);

		set_current_state(TASK_INTERRUPTIBLE);
		if (!kthread_should_stop())
			schedule();
		__set_current_state(TASK_RUNNING);
	}
	pr_warn("%s going out\n", __FUNCTION__);
	return 0;
}

void refill_task_start(struct kthread_pool *cbn_pool)
{
	wake_up_process(cbn_pool->refil);
}

static struct pool_elem *kthread_pool_alloc(struct kthread_pool *cbn_pool)
{
	struct pool_elem *elem = NULL;

	refill_task_start(cbn_pool);
	while (unlikely(list_empty(&cbn_pool->kthread_pool))) {
		pr_warn("pool is empty refill is to slow\n");
		POOL_ERR("pool is empty refill is to slow\n");
		return NULL;
	}

	spin_lock_irq(&cbn_pool->pool_lock);
	elem = list_first_entry(&cbn_pool->kthread_pool, struct pool_elem, list);
	list_del(&elem->list);
	++cbn_pool->refil_needed;
	spin_unlock_irq(&cbn_pool->pool_lock);
	POOL_PRINT("allocated %p [%p]\n", elem, elem->task);
	return elem;
}

struct pool_elem *kthread_pool_run(struct kthread_pool *cbn_pool, int (*func)(void *), void *data)
{
	struct pool_elem *elem = kthread_pool_alloc(cbn_pool);
	if (unlikely(!elem)) {
		pr_err("Failed to alloc elem\n");
		return ERR_PTR(-ENOMEM);
	}

	if (unlikely(elem->pool_task)) {
		pr_err("ERRORL task allocated twice....!!!! <%s>", elem->task->comm);
		POOL_ERR("ERRORL task allocated twice....!!!! <%s>", elem->task->comm);
	}

	elem->pool_task = func;
	elem->data = data;
	spin_lock_irq(&cbn_pool->running_lock);
	list_add(&elem->list, &cbn_pool->kthread_running);
	spin_unlock_irq(&cbn_pool->running_lock);
	POOL_PRINT("staring %s\n", elem->task->comm);
	wake_up_process(elem->task);
	return elem;
}

int __init cbn_kthread_pool_init(struct kthread_pool *cbn_pool)
{
	TRACE_PRINT("starting: %s", __FUNCTION__);
	INIT_LIST_HEAD(&cbn_pool->kthread_pool);
	INIT_LIST_HEAD(&cbn_pool->kthread_running);

	spin_lock_init(&cbn_pool->pool_lock);
	spin_lock_init(&cbn_pool->running_lock);

	cbn_pool->pool_slab = kmem_cache_create("pool-thread-cache",
						sizeof(struct pool_elem), 0, 0, NULL);

	cbn_pool->refil_needed = cbn_pool->pool_size;
	cbn_pool->refil = kthread_run(refil_thread, cbn_pool, "pool-cache-refill");

	//set_user_nice(cbn_pool->refil, MAX_NICE);
	return 0;
}

void __exit cbn_kthread_pool_clean(struct kthread_pool *cbn_pool)
{
	struct list_head *itr, *tmp;
	TRACE_PRINT("stopping: %s", __FUNCTION__);

	kthread_stop(cbn_pool->refil);

	list_for_each_safe(itr, tmp, &cbn_pool->kthread_pool) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
		list_del(itr);
		TRACE_PRINT("stopping pool %s", task->task->comm);
		kthread_stop(task->task);
		kmem_cache_free(cbn_pool->pool_slab, task);
	}

	list_for_each_safe(itr, tmp, &cbn_pool->kthread_running) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
		list_del(itr);
		TRACE_PRINT("stopping running %s", task->task->comm);
		kthread_stop(task->task);
		kmem_cache_free(cbn_pool->pool_slab, task);
	}
	kmem_cache_destroy(cbn_pool->pool_slab);
	TRACE_PRINT("stopping: elements freed");
}

