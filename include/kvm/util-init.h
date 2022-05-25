#ifndef KVM__UTIL_INIT_H
#define KVM__UTIL_INIT_H

struct kvm;

struct init_item {
	struct hlist_node n;
	const char *fn_name;
	int (*init)(struct kvm *);
};

int init_list__init(struct kvm *kvm);
int init_list__exit(struct kvm *kvm);

// 用于添加函数指针到全局变量。
int init_list_add(struct init_item *t, int (*init)(struct kvm *),
			int priority, const char *name);
int exit_list_add(struct init_item *t, int (*init)(struct kvm *),
			int priority, const char *name);

// 定义 1 个 init 函数，这个函数很重要，会触发添加初始化过程到 kvm 中。这些函数会在 main 函数之前运行。奇妙！！！
// How exactly does __attribute__((constructor)) work?
// It runs when a shared library is loaded, typically during program startup.
// That's how all GCC attributes are; presumably to distinguish them from function calls.
// GCC-specific syntax.
// Yes, this works in C and C++.
// No, the function does not need to be static.
// The destructor runs when the shared library is unloaded, typically at program exit.
#define __init_list_add(cb, l)						\
static void __attribute__ ((constructor)) __init__##cb(void)		\
{									\
	static char name[] = #cb;					\
	static struct init_item t;					\
	init_list_add(&t, cb, l, name);					\
}

#define __exit_list_add(cb, l)						\
static void __attribute__ ((constructor)) __init__##cb(void)		\
{									\
	static char name[] = #cb;					\
	static struct init_item t;					\
	exit_list_add(&t, cb, l, name);					\
}

// 定义对应的初始化方法。例如这里 core_init(kvm_init) 会定义 1 个函数 __init__kvm_init。
#define core_init(cb) __init_list_add(cb, 0)
#define base_init(cb) __init_list_add(cb, 2)
#define dev_base_init(cb)  __init_list_add(cb, 4)
#define dev_init(cb) __init_list_add(cb, 5)
#define virtio_dev_init(cb) __init_list_add(cb, 6)
#define firmware_init(cb) __init_list_add(cb, 7)
#define late_init(cb) __init_list_add(cb, 9)

#define core_exit(cb) __exit_list_add(cb, 0)
#define base_exit(cb) __exit_list_add(cb, 2)
#define dev_base_exit(cb) __exit_list_add(cb, 4)
#define dev_exit(cb) __exit_list_add(cb, 5)
#define virtio_dev_exit(cb) __exit_list_add(cb, 6)
#define firmware_exit(cb) __exit_list_add(cb, 7)
#define late_exit(cb) __exit_list_add(cb, 9)
#endif
