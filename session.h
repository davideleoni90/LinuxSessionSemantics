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
 * pages: pointer to the page descriptor of the first page in the buffer
 * order: the buffer session is made of 1<<order pages
 * lock: spinlock to be used to synchronize read and write operations on the
 * file during a session
 * position: offset with respect to the beginning of the file from which
 * the next I/O operation will take place during a session
 * filesize: number of bytes in the file
 * limit: size of the buffer
 * write: pointer to the former write operations for the file opened adopting
 * a session semantics
 * dirty: indicates that the session buffer has been modified, so as the session
 * gets closed the modifications have to be propagated to the original file
 * filename: string representing the filename in the user-space
 * link_to_list: list_head structure connecting the session object to the list of
 * all session objects
 */

struct session{
        void* buffer;
        struct page* pages;
        int order;
        spinlock_t lock;
        loff_t position;
        loff_t filesize;
        int limit;
        ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
        bool dirty;
        const char __user* filename;
        struct list_head link_to_list;
};

/*
 * Structure to keep track of all the file sessions opened in the system
 *
 * head: head of a doubly linked list where each element is a session object
 * representing an active I/O session
 */

struct sessions_list{
        int a;
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
void cleanup_sessions(void);
void init_sessions(struct sessions_list* sessions_list);

/*
 * FUNCTION PROTOTYPES - end
 */

#endif //SESSIONFILE_SESSION_H
