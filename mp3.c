#define LINUX
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include "mp3_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carolyn Murray");
MODULE_DESCRIPTION("CS-423 MP3");

#define DIRECTORY "mp3"
#define FILENAME "status"
#define BUFSIZE 128
#define SHAREDBUFSIZE (1024*512)
#define LONGSIZE (sizeof(long))
#define PAGESIZE 4096

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

/* structure initialization */
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

// List of processes
static mp3_task_struct mp3_task_struct_list;
static mp3_task_struct *tmp;
static struct list_head *pos, *q;


// Work variables
static struct workqueue_struct *queue;

// Semaphore Lock
static spinlock_t lock;

//Time Interval Manager
static unsigned long last_time = 0;

//Initializes Work Queue
static void _init_workqueue(void)
{
  queue = create_workqueue("mp3_workqueue");
}

// Registers work
static void _register_work(void)
{
	/*
	struct delayed_work *work = (struct delayed_work *)kmalloc(sizeof(struct delayed_work), GFP_KERNEL);
	if (work) {
		INIT_DELAYED_WORK((struct delayed_work *) work, update_runtimes);
		queue_delayed_work(queue, (struct delayed_work *) work, msecs_to_jiffies(50));
	} */
	printk(KERN_INFO "Registering Work\n");
}

// Deletes Work Queue
static void _del_workqueue(void)
{
  if (queue != NULL){
    flush_workqueue(queue);
    
    // Implement the following for any delayed work
    // cancel_delayed_work_sync(&work);
    destroy_workqueue(queue);
    queue = NULL;
    printk(KERN_ALERT "DELETED WORKQUEUE\n");
  }
}

/**
 * Get Current Time in jiffies
 *
 * RETURN current time in jiffies
**/
static unsigned long _get_time(void){
   struct timeval time;
   do_gettimeofday(&time);
   return usecs_to_jiffies(time.tv_sec * 1000000L + time.tv_usec);
}

/*
mp3_task_struct* _get_task_by_pid(unsigned int pid)
{
    	list_for_each_entry(tmp, &head, task_node) {
        	if (tmp->pid == pid) {
            		return tmp;
        	}
    	}	
    	return NULL;
}
*/
void mp3_register_process(char *buf)
{
	unsigned int pid;
	sscanf(buf, "%u", &pid); 
	printk(KERN_INFO "Registering process with pid, %d\n", pid);
	
	tmp = (mp3_task_struct *) kmalloc(sizeof(mp3_task_struct), GFP_KERNEL);
	tmp->pid = pid;
	tmp->util = 0;
	tmp->major_faults = 0;
	tmp->minor_faults = 0;
	tmp->task = find_task_by_pid(tmp->pid);

	spin_lock(&lock);
	list_add_tail(&(tmp->task_node), &(mp3_task_struct_list.task_node));
	spin_unlock(&lock);

	//TODO: call top half
	// If list is empty, then you want to record the time start
        if (list_empty(&mp3_task_struct_list.task_node)) {
                last_time = _get_time();
		_register_work();
        }
}

void mp3_unregister_process(char *buf)
{
	printk(KERN_INFO "called mp3_unregister_process\n");
}

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
	list_for_each_entry(tmp, &mp3_task_struct_list, task_node) {
		copied += sprintf(buf + copied, "PID: %u\tCPU_Util: %u\tMajor: %u\tMinor: %u\n", tmp->pid, tmp->util, tmp->major_faults, tmp->minor_faults);
	}
	spin_unlock_irqrestore(&lock, flags);
	
	buf[copied] = '\0';
	copy_to_user(buffer, buf, copied);
	return copied;
}

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

int __init mp3_init(void)
{
	printk(KERN_ALERT "LOADING MP2 MODULE\n");
	
	// Create directory in proc file system
	if ((proc_dir = proc_mkdir(DIRECTORY, NULL)) == NULL) {
		return -ENOMEM;
	}

	// Create entry in just created directory
	if ((proc_entry = proc_create(FILENAME, 066, proc_dir, &mp3_file)) == NULL) {
		remove_proc_entry(DIRECTORY, NULL);
		return -ENOMEM;
	}

	// Initialize spin lock, work queue
  	INIT_LIST_HEAD(&mp3_task_struct_list.task_node); 
	spin_lock_init(&lock);
	_init_workqueue();

	printk(KERN_ALERT "MP2 MODULE LOADED\n");
	return 0;
}

void __exit mp3_exit(void)
{
	printk(KERN_ALERT "UNLOADING MP2 MODULE\n");
	
	// Remove the proc file system entries associated with module
	remove_proc_entry(FILENAME, proc_dir);
   	remove_proc_entry(DIRECTORY, NULL);
	

	// Frees mp_struct memory   
  	list_for_each_safe(pos, q, &mp3_task_struct_list.task_node)
	{
    		tmp = list_entry(pos, mp3_task_struct, task_node);
		list_del(pos);
    		kfree(tmp);
  	}

	//Delete Workqueue
	_del_workqueue();

	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

module_init(mp3_init);
module_exit(mp3_exit);
