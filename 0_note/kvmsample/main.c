/*
https://blog.csdn.net/sdulibh/article/details/52093507

test.S 是用来给 kvm 测试的汇编代码

# A test code for kvmsample

.globl _start
    .code16
_start:
    xorw %ax, %ax  // %ax 寄存器置 0
    
loop1:
    out %ax, $0x10 // %ax 输出到 $0x10 io 端口
    inc %ax        // %ax 自增一次
    jmp loop1      // 再次数据
*/

/*
 * KVM API Sample.
 * author: Xu He Jie xuhj@cn.ibm.com
 */
#include <stdio.h>
#include <memory.h>
#include <sys/mman.h>
#include <pthread.h>
#include <linux/kvm.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>

#define KVM_DEVICE "/dev/kvm"
#define RAM_SIZE 512000000
#define CODE_START 0x1000
#define BINARY_FILE "test.bin"

// 代表 1 个虚拟机对象
struct kvm {
   int dev_fd; // 打开 /dev/kvm 的 fd
   int vm_fd;
   __u64 ram_size;
   __u64 ram_start;
   int kvm_version;
   struct kvm_userspace_memory_region mem;

   struct vcpu *vcpus; // 指向 vcpus 结构体数组
   int vcpu_number; // vcpu_number 数量
};

// 代表 1 个 cpu
struct vcpu {
    int vcpu_id; // vcpu_id 是对应的 id, 人工设置就好
    int vcpu_fd; // KVM_CREATE_VCPU
    pthread_t vcpu_thread; // 线程创建后对应这个结构体
    struct kvm_run *kvm_run; // kvm_run 结构体定义在 kvm.h
    int kvm_run_mmap_size; // 通过 KVM_GET_VCPU_MMAP_SIZE 命令获取的需要申请的内存
    struct kvm_regs regs; // 设置 regs，KVM_SET_SREGS，KVM_SET_REGS
    struct kvm_sregs sregs;
    void *(*vcpu_thread_func)(void *); // 启动1个cpu,就是运行1个线程起来
};

// 重设 vcpu 中的寄存器
void kvm_reset_vcpu (struct vcpu *vcpu) {
	if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &(vcpu->sregs)) < 0) {
		perror("can not get sregs\n");
		exit(1);
	}

	vcpu->sregs.cs.selector = CODE_START;
	vcpu->sregs.cs.base = CODE_START * 16;
	vcpu->sregs.ss.selector = CODE_START;
	vcpu->sregs.ss.base = CODE_START * 16;
	vcpu->sregs.ds.selector = CODE_START;
	vcpu->sregs.ds.base = CODE_START *16;
	vcpu->sregs.es.selector = CODE_START;
	vcpu->sregs.es.base = CODE_START * 16;
	vcpu->sregs.fs.selector = CODE_START;
	vcpu->sregs.fs.base = CODE_START * 16;
	vcpu->sregs.gs.selector = CODE_START;

    // 设置 sregs 到 vcpu
	if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0) {
		perror("can not set sregs");
		exit(1);
	}

	vcpu->regs.rflags = 0x0000000000000002ULL;
	vcpu->regs.rip = 0;
	vcpu->regs.rsp = 0xffffffff;
	vcpu->regs.rbp= 0;

    // 设置 regs 到 vcpu
	if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &(vcpu->regs)) < 0) {
		perror("KVM SET REGS\n");
		exit(1);
	}
}

// 1 个线程运行 1 个 cpu。启动 1 个 cpu 就是启动这个线程
void *kvm_cpu_thread(void *data) {
	struct kvm *kvm = (struct kvm *)data;
	int ret = 0;

    // 运行前 reset
	kvm_reset_vcpu(kvm->vcpus);

    // 开始运行
	while (1) {
		printf("KVM start run\n");
		ret = ioctl(kvm->vcpus->vcpu_fd, KVM_RUN, 0); // 开始运行
	
		if (ret < 0) {
			fprintf(stderr, "KVM_RUN failed\n");
			exit(1);
		}

		switch (kvm->vcpus->kvm_run->exit_reason) {
		case KVM_EXIT_UNKNOWN:
			printf("KVM_EXIT_UNKNOWN\n");
			break;
		case KVM_EXIT_DEBUG:
			printf("KVM_EXIT_DEBUG\n");
			break;
		case KVM_EXIT_IO:
			printf("KVM_EXIT_IO\n");
			printf("out port: %d, data: %d\n", 
				kvm->vcpus->kvm_run->io.port,  
				*(int *)((char *)(kvm->vcpus->kvm_run) + kvm->vcpus->kvm_run->io.data_offset)
				);
            // 手动 sleep 1s, 免得运行得太快
			sleep(1);
			break;
		case KVM_EXIT_MMIO:
			printf("KVM_EXIT_MMIO\n");
			break;
		case KVM_EXIT_INTR:
			printf("KVM_EXIT_INTR\n");
			break;
		case KVM_EXIT_SHUTDOWN:
			printf("KVM_EXIT_SHUTDOWN\n");
			goto exit_kvm;
			break;
		default:
			printf("KVM PANIC\n");
			goto exit_kvm;
		}
	}

exit_kvm:
	return 0;
}

void load_binary(struct kvm *kvm) {
    // 打开二进制文件, test.S
    int fd = open(BINARY_FILE, O_RDONLY);

    if (fd < 0) {
        fprintf(stderr, "can not open binary file\n");
        exit(1);
    }

    int ret = 0; // fd 当前代码的大小
    char *p = (char *)kvm->ram_start; // kvm 的代码写入

    while(1) {
        ret = read(fd, p, 4096);
        if (ret <= 0) {
            break;
        }
        printf("read size: %d", ret);
        p += ret;
    }
}

// 初始化 kvm 结构体
struct kvm *kvm_init(void) {
    struct kvm *kvm = malloc(sizeof(struct kvm));
    kvm->dev_fd = open(KVM_DEVICE, O_RDWR);

    if (kvm->dev_fd < 0) {
        perror("open kvm device fault: ");
        return NULL;
    }

    // 通过 ioctl 获取 kvm 版本
    kvm->kvm_version = ioctl(kvm->dev_fd, KVM_GET_API_VERSION, 0);

    return kvm;
}

// kvm 结构体的清理函数
void kvm_clean(struct kvm *kvm) {
    assert (kvm != NULL);
    close(kvm->dev_fd);
    free(kvm);
}

// 创建 kvm 入口
// ram_size 设置内存大小。通过 mmap 申请内存，并设置到 fd
int kvm_create_vm(struct kvm *kvm, int ram_size) {
    int ret = 0;
    kvm->vm_fd = ioctl(kvm->dev_fd, KVM_CREATE_VM, 0);

    if (kvm->vm_fd < 0) {
        perror("can not create vm");
        return -1;
    }

    kvm->ram_size = ram_size;
    kvm->ram_start =  (__u64)mmap(NULL, kvm->ram_size, 
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 
                -1, 0);

    if ((void *)kvm->ram_start == MAP_FAILED) {
        perror("can not mmap ram");
        return -1;
    }
    
    kvm->mem.slot = 0;
    kvm->mem.guest_phys_addr = 0;
    kvm->mem.memory_size = kvm->ram_size;
    kvm->mem.userspace_addr = kvm->ram_start;

    ret = ioctl(kvm->vm_fd, KVM_SET_USER_MEMORY_REGION, &(kvm->mem));

    if (ret < 0) {
        perror("can not set user memory region");
        return ret;
    }

    return ret;
}

// 关闭 vm_fd, 回收申请的内存
void kvm_clean_vm(struct kvm *kvm) {
    close(kvm->vm_fd);
    munmap((void *)kvm->ram_start, kvm->ram_size);
}

// 初始化 vcpu 结构体
struct vcpu *kvm_init_vcpu(struct kvm *kvm, int vcpu_id, void *(*fn)(void *)) {
    struct vcpu *vcpu = malloc(sizeof(struct vcpu));
    vcpu->vcpu_id = 0;
    vcpu->vcpu_fd = ioctl(kvm->vm_fd, KVM_CREATE_VCPU, vcpu->vcpu_id); // kvm 中创建 vcpu

    if (vcpu->vcpu_fd < 0) {
        perror("can not create vcpu");
        return NULL;
    }

    // 获取 vcpu mmap 区域的大小，以字节为单位
    vcpu->kvm_run_mmap_size = ioctl(kvm->dev_fd, KVM_GET_VCPU_MMAP_SIZE, 0);

    if (vcpu->kvm_run_mmap_size < 0) {
        perror("can not get vcpu mmsize");
        return NULL;
    }

    // vcpu->kvm_run_mmap_size 内存区域映射给 struct kvm_run 结构体
    printf("%d\n", vcpu->kvm_run_mmap_size);
    vcpu->kvm_run = mmap(NULL, vcpu->kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->vcpu_fd, 0);

    if (vcpu->kvm_run == MAP_FAILED) {
        perror("can not mmap kvm_run");
        return NULL;
    }

    // 线程函数
    vcpu->vcpu_thread_func = fn;
    return vcpu;
}

// 清理 vcpu 的内存, 关闭 vcpu_fd
void kvm_clean_vcpu(struct vcpu *vcpu) {
    munmap(vcpu->kvm_run, vcpu->kvm_run_mmap_size);
    close(vcpu->vcpu_fd);
}

// 运行 kvm 结构体
void kvm_run_vm(struct kvm *kvm) {
    int i = 0;

    // 根据 vcpu 数量创建线程
    for (i = 0; i < kvm->vcpu_number; i++) {
        if (pthread_create(&(kvm->vcpus->vcpu_thread), (const pthread_attr_t *)NULL, kvm->vcpus[i].vcpu_thread_func, kvm) != 0) {
            perror("can not create kvm thread");
            exit(1);
        }
    }

    // 等待所有线程允许完毕
    pthread_join(kvm->vcpus->vcpu_thread, NULL);
}

int main(int argc, char **argv) {
    int ret = 0;
    struct kvm *kvm = kvm_init();

    if (kvm == NULL) {
        fprintf(stderr, "kvm init fauilt\n");
        return -1;
    }

    if (kvm_create_vm(kvm, RAM_SIZE) < 0) {
        fprintf(stderr, "create vm fault\n");
        return -1;
    }

    // 设置 kvm->ram_start, 把编译的代码放到这里
    load_binary(kvm);

    // only support one vcpu now
    kvm->vcpu_number = 1;
    kvm->vcpus = kvm_init_vcpu(kvm, 0, kvm_cpu_thread); // 初始化 vcpu, 设置其启动线程 kvm_cpu_thread

    kvm_run_vm(kvm);

    // 退出时清理 kvm 和 kvm 对应的 vcpus
    kvm_clean_vm(kvm);
    kvm_clean_vcpu(kvm->vcpus);
    kvm_clean(kvm);
}
