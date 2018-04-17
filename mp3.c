#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include "mp3_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP3");

#define DIRECTORY "mp3"
#define FILENAME "status"

#define REGISTRATION     'R'
#define UNREGISTRATION   'U'

struct mp3_task_struct{
	struct task_struct *task;
	struct list_head task_node;
	
	unsigned int pid;
	unsigned long cpu_usage;
	unsigned long major_faults;
	unsigned long minor_faults;
	
}

/* structure initialization */
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;



void mp2_register_process(char *buf)
{
	printk(KERN_INFO "calling mp3_register_process\n");
}

void mp3_unregister_process(char *buf)
{
	printk(KERN_INFO "called mp3_unregister_process\n");
}

ssize_t mp3_read(struct file *file, char __user *buffer, size_t count, loff_t *data)
{
   
   	/* read callback function that runs when cat /proc/mp3/status
   	is called. */

	static int copied = 0;
	printf("inside mp3 read function\n");
	
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
			mp3_register_process(buf+3);
			break;
		case UNREGISTRATION:
			printk(KERN_INFO "Unregistering process case\n");
			mp3_unregister_process(buf+3);
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
	if ((proc_entry = proc_create(FILENAME, 066, proc_dir &mp3_file)) == NULL) {
		remove_proc_entry(DIRECTORY, NULL);
		return -ENOMEM;
	}
	printk(KERN_ALERT "MP2 MODULE LOADED\n");
}

void __exit mp3_exit(void)
{
	printk(KERN_ALERT "UNLOADING MP2 MODULE\n");
	
	// Remove the proc file system entries associated with module
	remove_proc_entry(FILENAME, proc_dir);
   	remove_proc_entry(DIRECTORY, NULL);
	
	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

module_init(mp3_init);
module_exit(mp3_exit);
