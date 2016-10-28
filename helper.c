/*
 * Set of helper functions
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <asm/unistd.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/ipc.h>
#include <linux/idr.h>
#include <linux/gfp.h>
#include <linux/rwsem.h>
#include <linux/sem.h>
#include <linux/slab.h>
#include <linux/gfp.h>\\
#include <linux/ipc_namespace.h>
#include <linux/ipc.h>
#include <linux/fdtable.h>
#include <asm-generic/current.h>

#define WP_X86 0x00010000

/*
 * FIND ADDRESS OF THE SYSTEM CALL TABLE AND SYS_NY_SYCALL- start
 */

/*
 * In case the symbol the kernel was compiled
 * with the "kallsyms" feature, the "kallsyms_lookup_name"
 * function can be used to retrieve the address
 * of a kernel symbol => we use it to find out
 * the address of the system call table, represented
 * by the variable "sys_call_table"
 *
 */

#if (defined CONFIG_KALLSYMS) && CONFIG_KALLSYMS==1
unsigned long* find_system_call_table(){
        return (unsigned long*)kallsyms_lookup_name("sys_call_table");
}

#else

/*
 * If the kallsyms feature is not available, we scan the whole kernel
 * address space until we find the system call table, i.e. an array
 * such that the element at position "__NR_CLOSE" (the index of the
 * "close" system call within the system call table) coincides with
 * the address of the system call "close" itself. This is due to the
 * fact that the system call table is an array of pointers to the
 * system call functions
 */


unsigned long* find_system_call_table(){

        /*
         * Pointer used to scan the kernel address space: the system call
         * table is an array whose elements contain the address (namely an
         * unsigned long) of a sys call each, that's why the type here is
         * pointer to unsigned long
         */

        unsigned long* sys_table;

        /*
         * The kernel address space starts from virtual address
         * PAGE_OFFSET(=3GB)
         */

        unsigned long offset = PAGE_OFFSET;

        /*
         * Scan the whole kernel address space until we find the
         * system call table as described above
         */

        while(offset < ULONG_MAX){

                /*
                 * Update the address pointed by the pointer to the
                 * next address in the kernel address space
                 */

                sys_table = (unsigned long *)offset;

                /*
                 * If the element at offset __NR_close from the address
                 * currently pointed by "sys_table" coincides with the
                 * address of the system call close, it means "sys_table"
                 * now points to the first byte of the system call table
                 * so return the address of the first byte, otherwise
                 * go to next address of the address space
                 */
                if(sys_table[__NR_close] == (unsigned long)sys_close)
                        return sys_table;
                offset += sizeof(void *);
        }
}

#endif

/*
 * FIND ADDRESS OF THE SYSTEM CALL TABLE - end
 */

/*
 * GET FILE OBJECT - start
 *
 * Return the object of type "struct file" associated to the given file
 * descriptor for the current process
 *
 * @fd: file descriptor associated to the opened file in the file table
 * of the current process
 */

struct file* get_file_from_descriptor(int fd){

        /*
         * Pointer to the structure designed to handle opened files of a process
         */

        struct files_struct *files;

        /*
         * File descriptors table of the current process
         */

        struct fdtable *table;

        /*
         * File object of the opened file
         */

        struct file *opened_file;

        /*
         * Get the open file table of the current process
         */

        files = current->files;

        /*
         * Get the table of file descriptors
         */

        table = files_fdtable(files);

        /*
         * Get the file object corresponding to the opened file
         */

        opened_file = table->fd[fd];

        /*
         * Return the file object
         */

        return opened_file;
}

/*
 * GET FILE OBJECT - end
 */


/*
 * ENABLE/DISABLE WRITE-PROTECTED MODE -start
 */

void disable_write_protected_mode(unsigned long* cr0){

        /*
         * Read value of the control register CR0: this value will be
         * restored after the write protected mode has been temporarily
         * disabled
         */

        *cr0=read_cr0();

        /*
         * Clear the sixth bit of the control register, i.e. disable the
         * write-protected mode
         */

        write_cr0(*cr0 &~WP_X86);
}

void enable_write_protected_mode(unsigned long* cr0){

        /*
         * Restore the value of the control register CR0
         */

        write_cr0(*cr0);
}

/*
 * ENABLE/DISABLE WRITE-PROTECTED MODE -end
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Davide Leoni");
