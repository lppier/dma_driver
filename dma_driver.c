#define DEBUG

#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direction.h>
#include <asm/uaccess.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/kfifo.h>
#include "dma_buffer_ioctl.h"

static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char __user *, size_t, loff_t *);
static int device_mmap(struct file *, struct vm_area_struct *);
static long device_ioctl(struct file *, unsigned int cmd, unsigned long arg);
static unsigned int device_poll(struct file *, poll_table *wait);

static int user_scatter_gather(struct file *filp, char __user * userbuf, size_t nbytes, struct page ** pages, struct scatterlist* sglist);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = device_read,
    .write = device_write,
    .mmap = device_mmap,
    .open = device_open,
    .release = device_release,
    .llseek = no_llseek,
    .unlocked_ioctl = device_ioctl,
    .poll = device_poll,
};

struct miscdevice dma_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "dma_driver",
    .fops = &fops,
    .mode = S_IRUGO | S_IWUGO,
};

struct driver_params
{
    struct page **pages1;
    struct scatterlist *sglist1;
    struct page **pages2;
    struct scatterlist *sglist2;
};

// -- Global Struct to hold pointers to buffers
struct driver_params d_params;

static int
device_open(struct inode *inodep, struct file *filp)
{
    pr_debug("%s\n", __func__);
    nonseekable_open(inodep, filp);
    return 0;
}

static int
device_release(struct inode *inodep, struct file *filp)
{
    pr_debug("%s\n", __func__);
    return 0;
}

static long
device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retcode = -ENOTTY;
    pr_debug("%s\n", __func__);

    switch (cmd) {

        case IOCTL_CMD_ALLOC_BUFFERS:
        {
            struct dma_buffer_ioctl param;
            int rc = copy_from_user(&param, (void*)arg, sizeof(param)); // copies pointers to buffers only ya, still have to get_user_pages to map
            if (rc!=0) return -EFAULT;

            pr_debug("ioctl_allocate_buffers %p %zu\n", param.buffer1, param.len1);
            pr_debug("ioctl_allocate_buffers %p %zu\n", param.buffer2, param.len2);

            // Temporarily, just try to map, unmap within user_scatter_gather for both buffers
            retcode = user_scatter_gather(filp, param.buffer1, param.len1, d_params.pages1, d_params.sglist1);
            retcode = user_scatter_gather(filp, param.buffer2, param.len2, d_params.pages2, d_params.sglist2);
            break;
        }

        case IOCTL_CMD_READ_BUFFER_STATUS:
        {
            struct dma_buffer_status_ioctl status;
            pr_debug("read buffer status reached\n");
            int rc = copy_from_user(&status, (void*)arg, sizeof(status));
            if (rc!=0) return -EFAULT;

            status.filled = 1;
            rc = copy_to_user((void*)arg, &status, sizeof(status));
            if (rc!=0) return -EFAULT;

            retcode = rc;
            break;
        }
    }

    return retcode;
}

static int
user_scatter_gather(struct file *filp, char __user *userbuf, size_t nbytes, struct page ** pages, struct scatterlist* sglist)
{
    int errcode = 0;
    int num_pages;
    int actual_pages;
    //struct page **pages = NULL;
    int page_idx;
    int offset;
    //struct scatterlist *sglist = NULL;
    int sg_count;
    struct miscdevice *miscdev = filp->private_data;    // filled in by misc_register

    if (nbytes==0)
        return 0;

    offset = offset_in_page(userbuf);

    num_pages = (offset + nbytes + PAGE_SIZE - 1) / PAGE_SIZE;

    pages = kmalloc(num_pages * sizeof(*pages), GFP_KERNEL);
    sglist = kmalloc(num_pages * sizeof(*sglist), GFP_KERNEL);
    if (!pages || !sglist) {    // okay if allocation for vmas failed
        errcode = -ENOMEM;
        goto cleanup_alloc;
    }

    // get_user_pages also locks the user-space buffer into memory
    down_read(&current->mm->mmap_sem);
    actual_pages = get_user_pages(
        #if LINUX_VERSION_CODE < KERNEL_VERSION(4,6,1)
        current, current->mm,
        #endif
        (unsigned long)userbuf, num_pages, 1, 0, pages, NULL);
    up_read(&current->mm->mmap_sem);

    if (actual_pages != num_pages) {
        pr_warning("get_user_pages returned %d / %d\n", actual_pages, num_pages);
        errcode = actual_pages < 0 ? actual_pages : -EAGAIN;
        // need to cleanup_pages for the case 0 < actual_pages < num_pages
        goto cleanup_pages;
    }

    pr_debug("get_user_pages returned %d\n", actual_pages);

    // populate sglist
    sg_init_table(sglist, num_pages);
    sg_set_page(&sglist[0], pages[0], PAGE_SIZE - offset, offset);
    for (page_idx=1; page_idx < num_pages-1; page_idx++) {
        sg_set_page(&sglist[page_idx], pages[page_idx], PAGE_SIZE, 0);
    }
    if (num_pages > 1) {
        sg_set_page(&sglist[num_pages-1], pages[num_pages-1],
            nbytes - (PAGE_SIZE - offset) - ((num_pages-2)*PAGE_SIZE), 0);
    }

    if (1) {
        for (page_idx=0; page_idx < num_pages; page_idx++) {
            struct scatterlist *sg = &sglist[page_idx];
            pr_debug("%d: %p %u\n", page_idx, pages[page_idx], sg->length);
        }
    }

    sg_count = dma_map_sg(miscdev->this_device, sglist, num_pages, DMA_FROM_DEVICE);
    if (sg_count==0) {
        pr_warning("dma_map_sg returned 0\n");
        errcode = -EAGAIN;
        goto cleanup_pages;
    }

    pr_debug("dma_map_sg returned %d\n", sg_count);

    // -- Intention is to put code from here on in the dma data ready interrupt servicing procedure
    // -- also need to initiate the DMA by setting the registers of the DMA device
    // Mapping to hardware internal descriptors should start here
    {
        struct scatterlist *sg;
        int sg_idx;

        for_each_sg(sglist, sg, sg_count, sg_idx) {
            unsigned long hwaddr = sg_dma_address(sg);
            unsigned int dmalen = sg_dma_len(sg);
            pr_debug("%d: %#08lx %u\n", sg_idx, hwaddr, dmalen);
        }
    }

    dma_unmap_sg(miscdev->this_device, sglist, num_pages, DMA_FROM_DEVICE);

cleanup_pages:
    for (page_idx=0; page_idx < actual_pages; page_idx++) {
        // alias page_cache_release has been removed
        put_page(pages[page_idx]);
    }

cleanup_alloc:
    kfree(sglist);
    kfree(pages);

    return errcode;
}

static ssize_t
device_read(struct file *filp, char __user *userbuf, size_t nbytes, loff_t *f_pos)
{
    return 0;
}

static ssize_t
device_write(struct file *filp, const char __user *userbuf, size_t nbytes, loff_t *f_pos)
{
    pr_debug("%s %zu\n", __func__, nbytes);
    return nbytes;
}

static int
device_mmap(struct file *filp, struct vm_area_struct *vma)
{
    return 0;
}

static unsigned int
device_poll(struct file *filp, poll_table *wait)
{
    return 0;
}

static int __init device_init(void)
{
    int rc;

    pr_debug("%s\n", __func__);
    rc = misc_register(&dma_device);
    if (rc!=0)
    {
        pr_warning("misc_register failed %d\n", rc);
    }

// -- Commented out as no DMA capable device to test against
//    if (dma_set_mask_and_coherent(dma_device.this_device, DMA_BIT_MASK(32)))
//    {
//        pr_warning("mymiscdev: No suitable DMA available\n");
//    }

    return rc;
}

static void __exit device_exit(void)
{
    pr_debug("%s\n", __func__);
    misc_deregister(&dma_device);
}

module_init(device_init)
module_exit(device_exit)

MODULE_DESCRIPTION("Simple DMA Driver");
MODULE_AUTHOR("Lim Pier");
MODULE_LICENSE("Dual MIT/GPL");

