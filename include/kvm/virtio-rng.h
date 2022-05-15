#ifndef KVM__RNG_VIRTIO_H
#define KVM__RNG_VIRTIO_H

struct kvm;

// Virtio RNG 是一种半虚拟化的随机数字生成器。Virtio RNG 会提供由虚拟机 (VM) 实例的主机生成的熵池中的随机数字。熵池会从系统收集随机信息，并使用该信息生成真正随机数字，以便用于敏感信息（如 SSH 密钥或唯一 ID）。
int virtio_rng__init(struct kvm *kvm);
int virtio_rng__exit(struct kvm *kvm);

#endif /* KVM__RNG_VIRTIO_H */
