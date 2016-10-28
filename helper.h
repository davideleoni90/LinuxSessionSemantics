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
 * Find address of the system call table
 */

unsigned long* find_system_call_table(void);

/*
 * Get file structure associated to given file descriptor
 * from the table of opened files of the current process
 */

struct file* get_file_from_descriptor(int fd);

void enable_write_protected_mode(unsigned long* cr0);
void disable_write_protected_mode(unsigned long* cr0);

#endif //BARRIERSYNCHRONIZATION_HELPER_C_H
