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
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/ipc_namespace.h>
#include <linux/types.h>
#include <asm-generic/current.h>
#include <asm-generic/pgtable.h>
#include "session.h"
#include "helper.h"

/*
 * INSERT/REMOVE MODULE - start
 */

/*
 * INSERT MODULE
 * -> replace "sys_open" with our custom "sys_open" that allows to open a file using
 * the "session semantics"
 */

int init_module(void) {

        /*
         * Value stored in the register CR0
         */

        unsigned long cr0;

        /*
         * Find the address of the system call table
         */

        system_call_table=find_system_call_table();

        /*
         * In case the CPU has WRITE-PROTECTED MODE enabled, even kernel
         * thread can't modify read-only pages, such those containing the
         * system call table => we first have to disable the WRITE-PROTECTED MODE
         * by clearing the corresponding bit (number 16) in the CR0 register.
         */

        disable_write_protected_mode(&cr0);

        /*
         * Find the address of the system call "sys_open": it's pointed by the
         * element system call table in position __NR_open
         * We need this address in order to restore the original system call
         * when the current module is removed
         */

        original_open=system_call_table[__NR_open];
        truncate_call=system_call_table[__NR_truncate];
        previous_open=system_call_table[__NR_open];

        /*
         * Replace original system call "open" with a custom version of it
         */

        system_call_table[__NR_open]=(unsigned long)sys_session_open;

        /*
         * Restore original value of register CR0
         */

        enable_write_protected_mode(&cr0);

        /*
         * Log message about our just inserted module
         */

        printk(KERN_INFO "Module \"session_module\" inserted: replaced system call at index %d",__NR_open);
        return 0;

}

/*
 * REMOVE MODULE
 * Restore the system call table to its original form removing our custom system  calls
 */

void cleanup_module(void) {

        unsigned long cr0;

        /*
         * In order to restore the system call table, the WRITE-PROTECTED
         * MODE has to be disabled again temporarily, so first save the
         * value of the CR0 register
         */

        disable_write_protected_mode(&cr0);

        /*
         * Restore system call table to its original shape
         */

        system_call_table[__NR_open]=original_open;

        /*
         * Restore value of register CR0
         */

        enable_write_protected_mode(&cr0);

        /*
         * Remove the data structures associated to the session semantics
         */

        //cleanup_sessions();

        printk(KERN_INFO "Module \"session_module\" removed: restored system call table\n");

}

/*
 * INSERT/REMOVE MODULE - end
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Davide Leoni");
