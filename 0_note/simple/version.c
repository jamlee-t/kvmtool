#include <linux/kvm.h>
#include <stdio.h>
#include <err.h>
#include <sys/ioctl.h>
#include <fcntl.h>

// 查看 kvm 对应的版本。是KVM_API_VERSION最终于2007年4月在Linux 2.6.22上更改为12，并在2.6.24中被锁定为稳定接口
int main(void)
{
    int kvm, ret;
    kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    ret = ioctl(kvm, KVM_GET_API_VERSION, NULL);
    if (ret == -1)
	    err(1, "KVM_GET_API_VERSION");
    if (ret != 12)
	    errx(1, "KVM_GET_API_VERSION %d, expected 12", ret);

    printf("current kvm version is %d \n", ret);

    ret = ioctl(kvm, KVM_CAP_NR_MEMSLOTS, NULL);
    printf("current kvm mem slot number is %d \n", ret);
}