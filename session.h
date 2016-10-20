#ifndef SESSIONFILE_SESSION_H
#define SESSIONFILE_SESSION_H

/*
 * When a process wants to start an I/O session relative to a certain file,
 * the following flag has to be bitwise-ORed with the other flags given in
 * the open system call
 */

#define SESSION_OPEN 00000004

/*
 * Pointer to the object that tracks all the file sessions active in the
 * system
 */

extern struct sessions_list* sessions_list;

/*
 * Structure to handle an I/O session on a file
 *
 * buffer: pointer to the content of the file during a session
 *
 * pages: pointer to the page descriptor of the first page in the buffer
 *
 * order: the buffer session is made of 1<<order pages
 *
 * mutex: semaphore to be used to synchronize read and write operations on the
 * file during a session; a spinlock can't be used because the functions used
 * to copy data to and from the session inside the critical sections may put
 * the process to sleep and this is not compatible with spinlocks
 *
 * position: offset with respect to the beginning of the file from which
 * the next I/O operation will take place during a session
 *
 * filesize: number of bytes in the file
 *
 * limit: size of the buffer
 *
 * f_ops_old: pointer to the structure containing pointers to original file operations
 * of an opened file; the legacy "write" operations is used to flush content of
 * the session when this is over and all the legacy operations are restored when
 * the session is removed
 *
 * f_ops_new: pointer to the structure containing pointers to the file operations in
 * session mode. This pointer is requested in order to properly release allocated
 * memory when the session is closed
 *
 * dirty: indicates that the session buffer has been modified, so as the session
 * gets closed the modifications have to be propagated to the original file
 *
 * link_to_list: list_head structure connecting the session object to the list of
 * all session objects
 *
 * filename: string representing the filename in the user-space
 *
 * file: pointer to the "struct file" associated to the opened file
 *
 * private: backup of the field "private_data" of the opened file. During a session,
 * this field is used to store the session object associated to the opened file
 */

struct session{
        void* buffer;
        struct page* pages;
        int order;
        struct mutex mutex;
        loff_t position;
        loff_t filesize;
        int limit;
        struct file_operations *f_ops_old;
        struct file_operations *f_ops_new;
        bool dirty;
        struct list_head link_to_list;
        char *filename;
        struct file *file;
        void* private;
};

/*
 * Structure to keep track of all the file sessions opened in the system
 *
 * head: head of a doubly linked list where each element is a session object
 * representing an active I/O session
 */

struct sessions_list{
        struct list_head sessions_head;
};

/*
 * Structure to keep track of each page of the session buffer
 *
 * buffer_head: head of the doubly linked list that keeps track of all pages in
 * a session buffer
 * page_address: virtual address of the page of the session buffer
 */

struct buffer_page{
        struct list_head buffer_head;
        void* page_address;
};

/*
 * FUNCTION PROTOTYPES - start
 */

extern asmlinkage long (*previous_open)(const char __user* filename,int flags,int mode);
extern asmlinkage long sys_session_open(const char __user* filename,int flags,int mode);
extern asmlinkage long (*truncate_call)(const char * path, long length);
void sessions_remove(void);
void sessions_list_init(struct sessions_list* sessions_list);

/*
 * FUNCTION PROTOTYPES - end
 */

#endif //SESSIONFILE_SESSION_H
