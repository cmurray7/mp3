#define LINUX
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include "mp3_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carolyn Murray");
MODULE_DESCRIPTION("CS-423 MP3");

#define DIRECTORY "mp3"
#define FILENAME "status"
#define SHAREDBUFSIZE (1024*512)
#define LONGSIZE (sizeof(long))
#define PAGESIZE 4096
#define PAGENUMBER 128

#define REGISTRATION     'R'
#define UNREGISTRATION   'U'

typedef struct mp3_task_struct{	
	
	struct task_struct *task;
	struct list_head task_node;
	
	unsigned int pid;
	unsigned long util;
	unsigned long major_faults;
	unsigned long minor_faults;
	
} mp3_task_struct;

/* initialization */

// Filesystem variables
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

// List of processes
static mp3_task_struct mp3_task_struct_list;
static mp3_task_struct *tmp;
static struct list_head *pos, *q;

// Work variables
static void update_runtimes(struct work_struct *w);
DECLARE_DELAYED_WORK(updater, update_runtimes);
static struct workqueue_struct *queue;
unsigned long delay;

// Memory buffer variables
unsigned long * memory_buffer;
int mb_ptr = 0;

// Character Driver variables
struct cdev chrdev;

// Lock
static spinlock_t lock;

/* Helper function to locate mp3_task_struct by PID */
static mp3_task_struct* _get_task_by_pid(unsigned int pid)
{
    	list_for_each_entry(tmp, &mp3_task_struct_list.task_node, task_node) {
        	if (tmp->pid == pid) {
            		return tmp;
        	}
    	}	
    	return NULL;
}

/**
* Delayed work function that update CPU stats and writes
* them to memory buffer. 
* Also, queues next update call.
**/
static void update_runtimes(struct work_struct *w)
{
	unsigned long utime, stime, major_faults, minor_faults;
	unsigned long all_major_faults = 0;
	unsigned long all_minor_faults = 0;
	unsigned long all_cpu_time = 0;

	printk(KERN_INFO "Update runtimes worker called\n");	
	
	spin_lock(&lock);
	list_for_each_entry(tmp, &mp3_task_struct_list.task_node, task_node) {
		
		if( get_cpu_use(tmp->pid, &minor_faults, &major_faults, &utime, &stime) ==  -1) {
			continue;
		}
		
		printk(KERN_INFO "%d: %lu, %lu, %lu\n", tmp->pid, minor_faults, major_faults, utime+stime);
		all_major_faults += major_faults;
		all_minor_faults += minor_faults;
		all_cpu_time += (utime+stime);
	}	
	spin_unlock(&lock);
	
	printk(KERN_INFO "Printing to memory buffer: %lu\t%lu\t%lu\t%lu\n", jiffies, all_minor_faults, all_major_faults, (unsigned long) jiffies_to_msecs(cputime_to_jiffies(all_cpu_time)));

	spin_lock(&lock);
	memory_buffer[mb_ptr++] = jiffies;
	memory_buffer[mb_ptr++] = all_minor_faults;
	memory_buffer[mb_ptr++] = all_major_faults;
	memory_buffer[mb_ptr++] = (unsigned long) jiffies_to_msecs(cputime_to_jiffies(all_cpu_time));
	spin_unlock(&lock);

	printk(KERN_INFO "Now memory buffer pointer is %d\n", mb_ptr);

	if (mb_ptr>(PAGENUMBER*PAGESIZE)/sizeof(unsigned long)) {
		printk(KERN_ALERT "Exceeded capacity of memory buffer, resetting to 0\n");
		mb_ptr = 0;
	}
	
	queue_delayed_work(queue, &updater, delay);

}
/**
* Device Driver Definitions
**/
static int mp3_open(struct inode *inode, struct file *filp){return 0;}
static int mp3_release(struct inode *inode, struct file *filp){return 0;}

static int mp3_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long pfn;
	int page_no;

	printk(KERN_INFO "MP3 MMAP\n");
	for (page_no = 0; page_no < PAGENUMBER; page_no++) {
		pfn = vmalloc_to_pfn((char *) (memory_buffer)+page_no*PAGESIZE);
		if (remap_pfn_range(vma, (unsigned long) (vma->vm_start)+page_no*PAGESIZE, pfn, PAGE_SIZE, PAGE_SHARED)) {
			printk(KERN_INFO "Remap fail\n");
			return -1;
		}
	}
	printk(KERN_INFO "MMAP Complete\n");
	return 0;
}

static struct file_operations mp3_fops = {
	.open = mp3_open,
	.release = mp3_release,
	.mmap = mp3_mmap,
	.owner = THIS_MODULE,
};

/**
* Takes in character buffer holding PID,
* initializes zero-usage altered PCB and
* adds to task list.
* If this is frist task on list, starts the 
* periodic workqueue job.  
**/ 
void mp3_register_process(char *buf)
{
	unsigned int pid;
	sscanf(buf, "%u", &pid); 
	printk(KERN_INFO "Registering process with pid, %u\n", pid);
	
	tmp = (mp3_task_struct *) kmalloc(sizeof(mp3_task_struct), GFP_KERNEL);
	tmp->pid = pid;
	tmp->util = 0;
	tmp->major_faults = 0;
	tmp->minor_faults = 0;
	tmp->task = find_task_by_pid(tmp->pid);

	spin_lock(&lock);

        if (list_empty(&mp3_task_struct_list.task_node)) {
                queue_delayed_work(queue, &updater, delay);
		printk(KERN_INFO "First thing in list--new job added\n");
        }

	list_add_tail(&(tmp->task_node), &(mp3_task_struct_list.task_node));
	spin_unlock(&lock);

	printk(KERN_INFO "Completed registration\n");

}

/**
* Takes in character buffer holding PID, 
* locates altered PCB of this PID from task
* list and removes it from list. 
* If that empties task list, stops the periodic 
* workqueue jobs
**/
void mp3_unregister_process(char *buf)
{
	unsigned int pid;
	mp3_task_struct* to_remove;
        sscanf(buf, "%u", &pid); 
	printk(KERN_INFO "Unregistering process with pid, %u\n", pid);

	spin_lock(&lock);
	to_remove = _get_task_by_pid(pid);
	if (to_remove) {
		list_del(&(to_remove->task_node));
		kfree(to_remove);
	}
	spin_unlock(&lock);

	if (list_empty(&mp3_task_struct_list.task_node)) {
		cancel_delayed_work(&updater);
		flush_workqueue(queue);
		printk(KERN_INFO "List empty -> Workqueue has been flushed and delayed work cancelled\n"); 		
	}

}

/**
* Proc Filesystem read callback
* Lists registered tasks to console
**/
ssize_t mp3_read(struct file *file, char __user *buffer, size_t count, loff_t *data)
{
   
   	/* read callback function that runs when cat /proc/mp3/status
   	is called. */
	unsigned long flags;
	static int copied = 0;
	char *buf;
	
	printk(KERN_INFO "inside mp3 read function\n");
	buf = (char *) kmalloc(count+1, GFP_KERNEL);
	if (copied) {
		copied = 0;
		return 0;
	}

	spin_lock_irqsave(&lock, flags);
	list_for_each_entry(tmp, &mp3_task_struct_list.task_node, task_node) {
		copied += sprintf(buf + copied, "PID: %u\tCPU_Util: %lu\tMajor: %lu\tMinor: %lu\n", tmp->pid, tmp->util, tmp->major_faults, tmp->minor_faults);
	}
	spin_unlock_irqrestore(&lock, flags);
	
	buf[copied] = '\0';
	copy_to_user(buffer, buf, copied);
	return copied;
}

/**
* Proc Filesystem write callback
* takes in a command and pid and 
* invokes register or unregister
*  behavior
**/
static ssize_t mp3_write(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
	char *buf;
	
	// Copy PID from user space
   	buf = (char *) kmalloc(count + 1, GFP_KERNEL);
	if(copy_from_user(buf, buffer, count)) {
		return -EFAULT;
   	}
	buf[count] = '\0';

	// Depending on command, execute register, deregister or yield behavior
	switch (buf[0]) {
		case REGISTRATION:
			printk(KERN_INFO "Registering process case\n");
			mp3_register_process(buf+2);
			break;
		case UNREGISTRATION:
			printk(KERN_INFO "Unregistering process case\n");
			mp3_unregister_process(buf+2);
			break;
		default:
			printk(KERN_INFO "default switch case\n");		
	}
	
	kfree(buf);
	return(count);
}

static const struct file_operations mp3_file = 
{
   .owner = THIS_MODULE,
   .read = mp3_read,
   .write = mp3_write,
};

int dev_no;
int __init mp3_init(void)
{

	printk(KERN_ALERT "LOADING MP2 MODULE\n");
	
	// Allocate virtually continuous memory buffer
	memory_buffer = (unsigned long*) vmalloc(PAGENUMBER * PAGESIZE);
  	memset(memory_buffer, 0, PAGENUMBER * PAGESIZE);

 	// check if enough memory is successfully allocated
  	if (!memory_buffer) {
    		printk(KERN_ALERT "VMALLOC ERROR\n");
  	}
  	printk("mem_buf is %lx\n", memory_buffer);
	if (memory_buffer==NULL) {
		printk(KERN_INFO "Memory Buffer = NULL\n");	
	}
	
	// Create directory in proc file system
	if ((proc_dir = proc_mkdir(DIRECTORY, NULL)) == NULL) {
		return -ENOMEM;
	}

	// Create entry in just created directory
	if ((proc_entry = proc_create(FILENAME, 066, proc_dir, &mp3_file)) == NULL) {
		remove_proc_entry(DIRECTORY, NULL);
		return -ENOMEM;
	}

	// Initialize task list, lock, workqueue, character driver
  	INIT_LIST_HEAD(&mp3_task_struct_list.task_node); 

	spin_lock_init(&lock);

	queue = create_workqueue("mp3_workqueue");
	delay = msecs_to_jiffies(50);

	cdev_init(&chrdev, &mp3_fops);
	dev_no = register_chrdev(0, "MP3", &mp3_fops); 
	printk(KERN_ALERT "CHARACTER DEVICE NUMBER: %d\n", dev_no);	

	printk(KERN_ALERT "MP2 MODULE LOADED\n");
	return 0;
}

void __exit mp3_exit(void)
{
	printk(KERN_ALERT "UNLOADING MP2 MODULE\n");
	
	// Remove the proc file system entries associated with module
	remove_proc_entry(FILENAME, proc_dir);
   	remove_proc_entry(DIRECTORY, NULL);
	
	spin_lock(&lock);
	// Frees mp_struct memory   
  	list_for_each_safe(pos, q, &mp3_task_struct_list.task_node)
	{
    		tmp = list_entry(pos, mp3_task_struct, task_node);
		list_del(pos);
    		kfree(tmp);
  	}
	spin_unlock(&lock);

	// Delete character driver from kernel
	unregister_chrdev(dev_no, "MP3");

	//Delete Workqueue
	cancel_delayed_work(&updater);
	flush_workqueue(queue);
	destroy_workqueue(queue);

	//V-freeing memory buffer space
	vfree((void*) memory_buffer);

	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

module_init(mp3_init);
module_exit(mp3_exit);
