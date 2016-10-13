#ifndef BARRIERSYNCHRONIZATION_HELPER_C_H
#define BARRIERSYNCHRONIZATION_HELPER_C_H

#define WP_X86 0x00010000

/*
 * Address of the system call table
 */

unsigned long* system_call_table;

/*
 * Address of the original system call "open"
 */

unsigned long* original_open;

asmlinkage long (*truncate_call)(const char * path, long length);
asmlinkage long (*previous_open)(const char __user* filename,int flags,int mode);

/*
 * Indexes of the system calls table entries modified by the module
 */

unsigned int restore[4];

void make_page_writable(pte_t* page);

/*
 * Find address of the system call table
 */

unsigned long* find_system_call_table(void);

void enable_write_protected_mode(unsigned long* cr0);
void disable_write_protected_mode(unsigned long* cr0);

#endif //BARRIERSYNCHRONIZATION_HELPER_C_H
