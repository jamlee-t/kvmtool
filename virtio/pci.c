#include "kvm/virtio-pci.h"

#include "kvm/ioport.h"
#include "kvm/kvm.h"
#include "kvm/virtio-pci-dev.h"
#include "kvm/irq.h"
#include "kvm/virtio.h"

#include <linux/virtio_pci.h>
#include <string.h>

static bool virtio_pci__specific_io_in(struct kvm *kvm, struct virtio_pci *vpci, u16 port,
					void *data, int size, int offset)
{
	u32 config_offset;
	int type = virtio__get_dev_specific_field(offset - 20, vpci->msix_enabled,
							0, &config_offset);
	if (type == VIRTIO_PCI_O_MSIX) {
		switch (offset) {
		case VIRTIO_MSI_CONFIG_VECTOR:
			ioport__write16(data, vpci->config_vector);
			break;
		case VIRTIO_MSI_QUEUE_VECTOR:
			ioport__write16(data, vpci->vq_vector[vpci->queue_selector]);
			break;
		};

		return true;
	} else if (type == VIRTIO_PCI_O_CONFIG) {
		u8 cfg;

		cfg = vpci->ops.get_config(kvm, vpci->dev, config_offset);
		ioport__write8(data, cfg);
		return true;
	}

	return false;
}

static bool virtio_pci__io_in(struct ioport *ioport, struct kvm *kvm, u16 port, void *data, int size)
{
	unsigned long offset;
	bool ret = true;
	struct virtio_pci *vpci;
	u32 val;

	vpci = ioport->priv;
	offset = port - vpci->base_addr;

	switch (offset) {
	case VIRTIO_PCI_HOST_FEATURES:
		val = vpci->ops.get_host_features(kvm, vpci->dev);
		ioport__write32(data, val);
		break;
	case VIRTIO_PCI_QUEUE_PFN:
		val = vpci->ops.get_pfn_vq(kvm, vpci->dev, vpci->queue_selector);
		ioport__write32(data, val);
		break;
	case VIRTIO_PCI_QUEUE_NUM:
		val = vpci->ops.get_size_vq(kvm, vpci->dev, vpci->queue_selector);
		ioport__write32(data, val);
		break;
		break;
	case VIRTIO_PCI_STATUS:
		ioport__write8(data, vpci->status);
		break;
	case VIRTIO_PCI_ISR:
		ioport__write8(data, vpci->isr);
		kvm__irq_line(kvm, vpci->pci_hdr.irq_line, VIRTIO_IRQ_LOW);
		vpci->isr = VIRTIO_IRQ_LOW;
		break;
	default:
		ret = virtio_pci__specific_io_in(kvm, vpci, port, data, size, offset);
		break;
	};

	return ret;
}

static bool virtio_pci__specific_io_out(struct kvm *kvm, struct virtio_pci *vpci, u16 port,
					void *data, int size, int offset)
{
	u32 config_offset, gsi, vec;
	int type = virtio__get_dev_specific_field(offset - 20, vpci->msix_enabled,
							0, &config_offset);
	if (type == VIRTIO_PCI_O_MSIX) {
		switch (offset) {
		case VIRTIO_MSI_CONFIG_VECTOR:
			vec = vpci->config_vector = ioport__read16(data);

			gsi = irq__add_msix_route(kvm,
						  vpci->pci_hdr.msix.table[vec].low,
						  vpci->pci_hdr.msix.table[vec].high,
						  vpci->pci_hdr.msix.table[vec].data);

			vpci->config_gsi = gsi;
			break;
		case VIRTIO_MSI_QUEUE_VECTOR: {
			vec = vpci->vq_vector[vpci->queue_selector] = ioport__read16(data);

			gsi = irq__add_msix_route(kvm,
						  vpci->pci_hdr.msix.table[vec].low,
						  vpci->pci_hdr.msix.table[vec].high,
						  vpci->pci_hdr.msix.table[vec].data);
			vpci->gsis[vpci->queue_selector] = gsi;
			break;
		}
		};

		return true;
	} else if (type == VIRTIO_PCI_O_CONFIG) {
		vpci->ops.set_config(kvm, vpci->dev, *(u8 *)data, config_offset);

		return true;
	}

	return false;
}

static bool virtio_pci__io_out(struct ioport *ioport, struct kvm *kvm, u16 port, void *data, int size)
{
	unsigned long offset;
	bool ret = true;
	struct virtio_pci *vpci;
	u32 val;

	vpci = ioport->priv;
	offset = port - vpci->base_addr;

	switch (offset) {
	case VIRTIO_PCI_GUEST_FEATURES:
		val = ioport__read32(data);
		vpci->ops.set_guest_features(kvm, vpci, val);
		break;
	case VIRTIO_PCI_QUEUE_PFN:
		val = ioport__read32(data);
		vpci->ops.init_vq(kvm, vpci->dev, vpci->queue_selector, val);
		break;
	case VIRTIO_PCI_QUEUE_SEL:
		vpci->queue_selector	= ioport__read16(data);
		break;
	case VIRTIO_PCI_QUEUE_NOTIFY:
		val			= ioport__read16(data);
		vpci->ops.notify_vq(kvm, vpci->dev, val);
		break;
	case VIRTIO_PCI_STATUS:
		vpci->status		= ioport__read8(data);
		break;
	default:
		ret = virtio_pci__specific_io_out(kvm, vpci, port, data, size, offset);
		break;
	};

	return ret;
}

static struct ioport_operations virtio_pci__io_ops = {
	.io_in	= virtio_pci__io_in,
	.io_out	= virtio_pci__io_out,
};

static void callback_mmio(u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	struct virtio_pci *vpci = ptr;
	void *table = &vpci->pci_hdr.msix.table;

	vpci->msix_enabled = 1;
	if (is_write)
		memcpy(table + addr - vpci->msix_io_block, data, len);
	else
		memcpy(data, table + addr - vpci->msix_io_block, len);
}

int virtio_pci__signal_vq(struct kvm *kvm, struct virtio_pci *vpci, u32 vq)
{
	kvm__irq_line(kvm, vpci->gsis[vq], VIRTIO_IRQ_HIGH);

	return 0;
}

int virtio_pci__signal_config(struct kvm *kvm, struct virtio_pci *vpci)
{
	kvm__irq_line(kvm, vpci->config_gsi, VIRTIO_IRQ_HIGH);

	return 0;
}

int virtio_pci__init(struct kvm *kvm, struct virtio_pci *vpci, void *dev,
			int device_id, int subsys_id)
{
	u8 pin, line, ndev;

	vpci->dev = dev;
	vpci->msix_io_block = pci_get_io_space_block();

	vpci->base_addr = ioport__register(IOPORT_EMPTY, &virtio_pci__io_ops, IOPORT_SIZE, vpci);
	kvm__register_mmio(kvm, vpci->msix_io_block, 0x100, callback_mmio, vpci);

	vpci->pci_hdr = (struct pci_device_header) {
		.vendor_id		= PCI_VENDOR_ID_REDHAT_QUMRANET,
		.device_id		= device_id,
		.header_type		= PCI_HEADER_TYPE_NORMAL,
		.revision_id		= 0,
		.class			= 0x010000,
		.subsys_vendor_id	= PCI_SUBSYSTEM_VENDOR_ID_REDHAT_QUMRANET,
		.subsys_id		= subsys_id,
		.bar[0]			= vpci->base_addr | PCI_BASE_ADDRESS_SPACE_IO,
		.bar[1]			= vpci->msix_io_block |
					PCI_BASE_ADDRESS_SPACE_MEMORY |
					PCI_BASE_ADDRESS_MEM_TYPE_64,
		/* bar[2] is the continuation of bar[1] for 64bit addressing */
		.bar[2]			= 0,
		.status			= PCI_STATUS_CAP_LIST,
		.capabilities		= (void *)&vpci->pci_hdr.msix - (void *)&vpci->pci_hdr,
	};

	vpci->pci_hdr.msix.cap = PCI_CAP_ID_MSIX;
	vpci->pci_hdr.msix.next = 0;
	vpci->pci_hdr.msix.table_size = (VIRTIO_PCI_MAX_VQ + 1) | PCI_MSIX_FLAGS_ENABLE;
	vpci->pci_hdr.msix.table_offset = 1; /* Use BAR 1 */
	vpci->config_vector = 0;

	if (irq__register_device(VIRTIO_ID_RNG, &ndev, &pin, &line) < 0)
		return -1;

	vpci->pci_hdr.irq_pin	= pin;
	vpci->pci_hdr.irq_line	= line;
	pci__register(&vpci->pci_hdr, ndev);

	return 0;
}
