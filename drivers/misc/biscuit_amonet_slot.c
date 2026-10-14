// SPDX-License-Identifier: GPL-2.0
/*
 * Export the amonet v2 active slot as androidboot.slot_suffix.
 *
 * amonet v2 keeps compact A/B metadata at misc+0x360, overlaying the
 * bootloader_message slot_suffix field, while the Fire OS LK reports a stale
 * slot. Android's first-stage init resolves slotselect fstab entries from
 * androidboot.slot_suffix in /proc/cmdline, so read the record before init
 * runs and append the suffix there.
 */

#define pr_fmt(fmt) "biscuit-amonet-slot: " fmt

#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/string.h>

#define MISC_PARTITION_NAME		"misc"
#define AMONET_BCB_OFFSET		0x360
#define AMONET_BCB_MAGIC		"ABB"
#define AMONET_BCB_MAGIC_SIZE		3
#define AMONET_BCB_VERSION		1
#define AMONET_BCB_SLOT_A_OFFSET	5
#define AMONET_BCB_SLOT_B_OFFSET	6
#define AMONET_BCB_SIZE			7
#define AMONET_BCB_SLOT_PRIORITY_MASK	0x0f
#define AMONET_BCB_SLOT_TRIES_MASK	0x70
#define AMONET_BCB_SLOT_SUCCESS_MASK	0x80

#define MISC_WAIT_STEP_MS		20
#define MISC_WAIT_MAX_MS		5000

static bool __init slot_is_bootable(u8 metadata)
{
	return (metadata & AMONET_BCB_SLOT_SUCCESS_MASK) ||
	       (metadata & AMONET_BCB_SLOT_TRIES_MASK);
}

/* Same selection rules as the CM14 fs_mgr amonet v2 BCB fallback. */
static int __init amonet_bcb_to_slot(const u8 *bcb)
{
	int slot = 0;

	if (bcb[0] != '\0' ||
	    memcmp(bcb + 1, AMONET_BCB_MAGIC, AMONET_BCB_MAGIC_SIZE) ||
	    bcb[4] != AMONET_BCB_VERSION)
		return -EINVAL;

	if ((bcb[AMONET_BCB_SLOT_B_OFFSET] & AMONET_BCB_SLOT_PRIORITY_MASK) >
	    (bcb[AMONET_BCB_SLOT_A_OFFSET] & AMONET_BCB_SLOT_PRIORITY_MASK))
		slot = 1;

	if (!slot_is_bootable(bcb[AMONET_BCB_SLOT_A_OFFSET + slot])) {
		slot = 1 - slot;
		if (!slot_is_bootable(bcb[AMONET_BCB_SLOT_A_OFFSET + slot]))
			return -ENOENT;
	}

	return slot;
}

static int __init match_misc_partition(struct device *dev, const void *data)
{
	struct hd_struct *part;

	if (dev->type != &part_type)
		return 0;

	part = dev_to_part(dev);
	return part->info && !strcmp(part->info->volname, data);
}

static int __init read_amonet_bcb(u8 *bcb)
{
	struct block_device *bdev;
	struct device *dev = NULL;
	struct page *page;
	dev_t devt;
	int waited;
	int ret = 0;

	/* eMMC partitions are scanned asynchronously; wait for misc. */
	for (waited = 0; waited <= MISC_WAIT_MAX_MS; waited += MISC_WAIT_STEP_MS) {
		dev = class_find_device(&block_class, NULL, MISC_PARTITION_NAME,
					match_misc_partition);
		if (dev)
			break;
		msleep(MISC_WAIT_STEP_MS);
	}
	if (!dev)
		return -ENODEV;

	devt = dev->devt;
	put_device(dev);

	bdev = blkdev_get_by_dev(devt, FMODE_READ, NULL);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	page = read_mapping_page(bdev->bd_inode->i_mapping,
				 AMONET_BCB_OFFSET >> PAGE_SHIFT, NULL);
	if (IS_ERR(page)) {
		ret = PTR_ERR(page);
		goto out;
	}

	memcpy(bcb, (u8 *)kmap(page) + (AMONET_BCB_OFFSET & ~PAGE_MASK),
	       AMONET_BCB_SIZE);
	kunmap(page);
	put_page(page);
out:
	blkdev_put(bdev, FMODE_READ);
	return ret;
}

static int __init biscuit_amonet_slot_init(void)
{
	u8 bcb[AMONET_BCB_SIZE];
	char *cmdline;
	int slot;
	int ret;

	if (strstr(saved_command_line, "androidboot.slot_suffix=")) {
		pr_info("slot suffix already set, leaving cmdline unchanged\n");
		return 0;
	}

	ret = read_amonet_bcb(bcb);
	if (ret) {
		pr_err("cannot read misc+0x%x: %d\n", AMONET_BCB_OFFSET, ret);
		return 0;
	}

	slot = amonet_bcb_to_slot(bcb);
	if (slot < 0) {
		pr_err("no valid amonet v2 slot record: %d\n", slot);
		return 0;
	}

	cmdline = kasprintf(GFP_KERNEL, "%s androidboot.slot_suffix=_%c",
			    saved_command_line, 'a' + slot);
	if (!cmdline)
		return 0;

	/* The original buffer is memblock-allocated and stays referenced. */
	saved_command_line = cmdline;
	pr_info("active slot _%c\n", 'a' + slot);
	return 0;
}
late_initcall_sync(biscuit_amonet_slot_init);
