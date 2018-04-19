# mp3

To run:
```
insmod mp3.ko
cat /proc/devices/
mknod node c <insert device number> 0
...
rmmod mp3
```

Implementation decisions:

In this MP, a I/O and CPU profiler is implemented in order to monitor page fault rate and CPU utilization of different types of workloads. In order to do this, though, the profiler must have access kernel data, which cannot be efficiently accessed from user-space if utilizing the common context switch paradigm to access that data. This overhead can be addressed by mapping a set of physical pages allocatedin the kernel space to the virtual address space of the user-level process, which the user proces can access without additional overhead. 

Registration and deregistration of processes was done through the proc filesystem, (un)registering (from)to a linked task list of altered PCBs. This was implemented similarly to the previous MP's implementations. 

In order to monitory activity, I implemented a delayed work queue. When a first process landed on the task list, a work queue job was placed on the work queue (to be run 50 milliseconds from that assignment). The work job traverses the task list, accumulating number of major page faults, number of minor faults and CPU utilization (in milliseconds) fo the tasks registered. It also records the jiffies at the time of traversal. The work job then places those new statistics into a memory buffer that was mapped to allow access by the user-level monitor. Finally, this job placed another identical job onto the work queue to be run 50 milliseconds from that call. This enforced the sampling rate of 20 checkups per second.

A character device driver was used to map the shared memory to it's address space. 
