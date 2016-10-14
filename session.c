#include <linux/fs.h>
#include <linux/gfp.h>
#include <asm-generic/current.h>
#include <linux/fdtable.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/list.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <asm-generic/pgtable.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/compiler.h>
#include <linux/radix-tree.h>
#include <asm-generic/bug.h>
#include <linux/vmstat.h>
#include <linux/file.h>
#include <linux/backing-dev.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include "session.h"

extern asmlinkage long (*truncate_call)(const char *path, long length);

/*
 * GLOBAL VARIABLES - start
 */

/*
 * Pointer to new structure for file operations
 */

struct file_operations *f_ops;

/*
 * Pointer to the descriptor of the first page allocated to read the file
 */

struct page *first_page;

/*
 * GLOBAL VARIABLES - end
 */

/*
 * REMOVE SESSION - start
 *
 * Free the buffer associated to the session and th remove the session
 * object itself
 */

void remove_session(struct session *session) {

        printk(KERN_INFO "removing session %lu\n", session);

        /*
         * Index to scan through the page descriptors
         */

        int i = 0;

        /*
         * Acquire lock on the session object
         */

        //spin_lock(&session->lock);

        /*
         * Set the "mapping" field of the pages of the buffer
         * to NULL, otherwise "free_pages" complains because
         * pages are still in used
         */

        //printk(KERN_INFO "start loop session %lu %d\n", session,session->order);

        //for (i = 0; i < (1 << (session->order)); i++) {

        /*
         * Current page descriptor
         */

        //struct page *page;

        /*
         * Get page descriptor of current page
         */

        //if (!page) {
        //printk(KERN_INFO "NULL PAGE\n");
        //}
        //else {
        //page = session->pages + i;
        //printk(KERN_INFO "page %lu\n", page);

        /*
         * Set mapping to NULL for the current page
         */

        //page->mapping = NULL;

        //printk(KERN_INFO "page:%d\n", i);

        //}
        //}

        /*
         * Release buffer
         */

        //free_pages(session->buffer, session->order);

        /*
         * Release lock on the session object
         */

        //spin_unlock(&session->lock);

        /*
         * Remove the session object itself
         */

        //printk(KERN_INFO "done with sesssion %lu", session);
        //kfree(session);
}

/*
 * REMOVE SESSION - end
 */

/*
 * INITIALIZE THE SESSIONS LIST - start
 *
 * Initialize the list_head of the object used to keep track of all
 * the active file sessions
 *
 * @sessions_list: pointer to object to be initialized
 */

void init_sessions(struct sessions_list *sessions) {

        INIT_LIST_HEAD(&(sessions->sessions_head));
}

/*
 * INITIALIZE THE SESSIONS LIST - end
 */

/*
 * FILE OPERATIONS IN THE SESSION SEMANTICS - start
 *
 * -> file operations to be used when a file is opened using a session semantics
 *
 * 1-session_read
 * 2-session_write
 * 3-session_llseek
 * 4-session_close
 */

/*
 * According to the session semantics, file readings have to be redirected to the
 * buffer where the content of the original file has been copied => reading a file
 * simply means copying the requested number of bytes from the session buffer to
 * the user-space buffer, starting from the offset indicated in the session object
 *
 * In order to avoid race conditions among processes sharing the same struct file
 * associated to the opened file, a spinlock has to be acquired first
 *
 * @file: pointer to the file object to be read
 * @buf: user-space buffer where the read content has to be placed
 * @size: number of bytes to read from the file
 * @offset: starting point of the read operation w.r.t to the beginning of the file;
 * this parameter corresponds to the file pointer used in usual I/O operations, so
 * we ignore it
 *
 * Returns number of bytes copied to given buffer or -EINVAL in case the file does
 * not contain a reference to the session object
 *
 */


ssize_t session_read(struct file *file, char __user *buf, size_t size, loff_t *offset) {

        /*
         * Object representing the current session
         */

        struct session *session;

        /*
         * File pointer within the current session: this is may be different from the offset
         * received as last parameter of this function
         */

        loff_t file_pointer;

        /*
         * Virtual address corresponding to the current offset of the opened file
         */

        void *src;

        /*
         * Return value
         */

        ssize_t ret;

        /*
         * Bytes left to read at each iteration in case the requested size is
         * larger than PAGE_SIZE
         */

        int left_to_read;

        /*
         * Get the session object from the opened file
         */

        session = file->private_data;

        /*
         * Return -EINVAL if the session object is not set
         */

        if (!session) {
                printk(KERN_INFO "SESSION SEMANTICS->session_read returned an error: %d\n", -EINVAL);
                return -EINVAL;
        }

        /*
         * Acquire the lock over the session object
         */

        spin_lock(&session->lock);

        /*
         * Get the value of the file pointer for the current session
         */

        file_pointer = session->position;
        printk(KERN_INFO "Position before:%d\n", file_pointer);

        /*
         * Get the position within the file corresponding to the file pointer
         */

        src = session->buffer + file_pointer * sizeof(void);

        /*
         * If the number of bytes requested is beyond the limit of the file,
         * shrink the "size" parameter to a suitable value
         */

        if (file_pointer + size > session->filesize) {
                size = session->filesize - file_pointer;
        }

        /*
         * If the requested size is bigger than PAGE_SIZE, it is necessary to
         * split the reading operations in chunks of 4095 bytes each
         */

        if (size >= PAGE_SIZE) {

                /*
                 * Initially all the bytes have to be read
                 */

                left_to_read = size;

                /*
                 * With the following loop we read a number
                 * of bytes that is multiple of 4095.
                 * Then we read the remaining bytes
                 */

                do {

                        /*
                         * Copy chunk of bytes
                         */

                        ret = 4095 - copy_to_user(buf, src, 4095);

                        /*
                         * Update positions in the buffers:
                         * copy_to_users returns the number
                         * of bytes not copied
                         */

                        buf = buf + ret * sizeof(void);
                        src = src + ret * sizeof(void);

                        /*
                         * Update counter
                         */

                        left_to_read = size - ret;
                } while (left_to_read >= 4095);

                /*
                 * Check if the whole file was read
                 */

                if (left_to_read) {

                        /*
                         * Read remaining bytes
                         */

                        ret = copy_to_user(buf, src, left_to_read);

                        /*
                         * Set ret to the number of bytes read
                         */

                        if (!ret) {

                                /*
                                 * The whole file was read
                                 */

                                ret = size;
                        }
                        else {

                                /*
                                 * Part of the file was not read
                                 */

                                ret = size - ret;
                        }
                }
                else {
                        /*
                         * The whole file was read
                         */

                        ret = size;
                }
        }
        else {

                /*
                 * Copy the requested number of bytes from the session buffer to
                 * the user-space buffer. The function "copy_to_user" return the
                 * number of bytes that COULD NOT BE COPIED, so we subtract this
                 * from the requested number of bytes as result
                 */

                ret = size - copy_to_user(buf, src, size);
        }

        /*
         * Move the position of the file pointer in the session object
         * forward by the number of bytes copied from session buffer to
         * user-space buffer
         */

        session->position += (loff_t) ret;
        printk(KERN_INFO "pos:%d", session->position);

        /*
         * Release the lock over the session object
         */

        spin_unlock(&session->lock);

        /*
         * Return the number of bytes copied
         */

        printk(KERN_INFO "SESSION SEMANTICS->session_read read %d bytes\n", ret);
        return ret;
}

/*
 * According to the session semantics, writing a file means simply copying the content of
 * the given buffer into the buffer when the opened file is stored; also the
 * file pointer of the session has to be updated.
 *
 * In order to avoid race conditions among processes sharing the same struct file
 * associated to the opened file, a spinlock has to be acquired first
 *
 * @file: pointer to the file object to be written
 * @buf: user-space buffer containing content to be written
 * @size: number of bytes to write into the file
 * @offset: starting point of the write operation w.r.t to the beginning of the file;
 * this parameter corresponds to the file pointer used in usual I/O operations, so
 * we ignore it
 *
 * Returns number of bytes written into the buffer associated to the opened file or
 * -EINVAL in case the file does not contain a reference to the session object
 */

ssize_t session_write(struct file *file, const char __user *buf, size_t size, loff_t *offset) {

        /*
         * Object representing the current session
         */

        struct session *session;

        /*
         * File pointer within the current session: this is may be different from the offset
         * received as last parameter of this function
         */

        loff_t file_pointer;

        /*
         * Virtual address corresponding to the current offset of the opened file
         */

        void *dest;

        /*
         * Bytes left to write in the loop in case the size is larger than PAGE_SIZE
         */

        int left_to_write;

        /*
         * Return value;
         */

        ssize_t ret;

        /*
         * Limit for the bytes to be written; the number of pages allocated is well
         * defined
         */

        int limit;

        /*
         * Get the session object from the opened file
         */


        session = file->private_data;

        /*
         * Return -EINVAL if the session object is not set
         */

        if (!session) {
                printk(KERN_INFO "SESSION SEMANTICS->session_write returned an error: %d\n", -EINVAL);
                return -EINVAL;
        }

        /*
         * Acquire the lock over the session object
         */

        spin_lock(&session->lock);

        /*
         * Get the value of the file pointer for the current session
         */

        file_pointer = session->position;

        /*
         * Get the position within the file corresponding to the file pointer
         */

        dest = session->buffer + file_pointer * sizeof(void);

        /*
         * Check if the write is beyond the limit of the buffer: if so shrink
         * the amount of bytes to write to a suitable size
         */

        if (file_pointer + size > session >= limit) {
                size = limit - file_pointer;
        }

        printk(KERN_INFO "SIZE:%d\n", size);

        /*
         * If the requested size is bigger than PAGE_SIZE, it is necessary to
         * split the writing operations in chunks of 4095 bytes each
         */

        if (size >= PAGE_SIZE) {

                /*
                 * Initially all the bytes have to write
                 */

                left_to_write = size;

                /*
                 * With the following loop we read a number
                 * of bytes that is multiple of 4095.
                 * Then we read the remaining bytes
                 */

                do {
                        /*
                         * Copy chunk of bytes
                         */

                        ret = 4095 - copy_from_user(dest, buf, 4095);

                        /*
                         * Update positions in the buffers:
                         * copy_to_users returns the number
                         * of bytes not copied
                         */

                        dest = dest + ret * sizeof(void);
                        buf = buf + ret * sizeof(void);

                        /*
                         * Update counter
                         */

                        left_to_write = size - ret;
                        printk(KERN_INFO "LEft:%d\n", left_to_write);
                } while (left_to_write >= 4095);

                /*
                 * Check if the whole file was written
                 */

                if (left_to_write) {

                        printk(KERN_INFO "LEft:%d\n", left_to_write);

                        /*
                         * Read remaining bytes
                         */

                        ret = copy_from_user(dest, buf, left_to_write);

                        /*
                         * Set ret to the number of bytes written
                         */

                        if (!ret) {

                                /*
                                 * The whole file was written
                                 */

                                ret = size;
                        }
                        else {

                                /*
                                 * Part of the file was not written
                                 */

                                ret = size - ret;
                        }
                }
                else {

                        printk(KERN_INFO "LEft:%d\n", left_to_write);

                        /*
                         * The whole file was written
                         */

                        ret = size;
                }
        }
        else {

                /*
                 * Copy the requested number of bytes from the user-space buffer to
                 * the session buffer. The function "copy_from_user" return the
                 * number of bytes that COULD NOT BE COPIED, so we subtract this
                 * from the requested number of bytes as result
                 */

                ret = size - copy_from_user(dest, buf, size);
        }

        /*
         * Move the position of the file pointer in the session object
         * forward by the number of bytes copied from user-space to
         * session buffer
         */

        //session->position+=(loff_t)ret;

        /*
         * If the file pointer now points to an offset greater than the offset
         * stored in the "filesize" field it means the file changed its size
         * so we have to update the "filesize" field
         */

        printk(KERN_INFO "Ok\n");

        if (session->position > session->filesize)
                session->filesize = session->position;

        /*
         * Set the "dirty" flag of the session buffer because it has been
         * modified so there are some updates to transfer to the original
         * file
         */

        session->dirty = true;

        /*
         * Relase the lock over the session object
         */

        spin_unlock(&session->lock);

        /*
         * Return the number of bytes copied
         */

        printk(KERN_INFO "SESSION SEMANTICS->session_write wrote %d bytes\n", ret);
        return ret;
}

/*
 * According to the session semantics, seeking a file means simply changing the field "position"
 * inside the session object, namely moving the session file pointer accordinf to the
 * mode specified as parameter
 *
 * In order to avoid race conditions among processes sharing the same struct file
 * associated to the opened file, a spinlock has to be acquired first
 *
 * @file: pointer to the file object to be written
 * @offset: the size of the shift of the file pointer
 * @origin: position within the session buffer from which the shift has to take place;
 * values for this parameter have to be interpreted as with regular lseek, namely
 *
 * SEEK_SET: origin coincides with the beginning of the buffer
 * SEEK_CUR: origin coincides with the current values of the session file pointer
 * SEEK_END: origin coincides with the last byte of the buffer
 *
 * Returns the new value for the session file pointer or -EINVAL in case the
 * requested parameters are not compatible with the buffer associated to the session
 */

loff_t session_llseek(struct file *file, loff_t offset, int origin) {

        /*
         * Object representing the current session
         */

        struct session *session;

        /*
         * File pointer within the current session: this is may be different from the offset
         * received as last parameter of this function
         */

        loff_t file_pointer;

        /*
         * Virtual address corresponding to the current offset of the opened
         * file
         */

        void *dest;

        /*
         * Get the session object from the opened file
         */

        session = file->private_data;

        /*
         * Return -EINVAL if the session object is not set
         */

        if (!session) {
                printk(KERN_INFO "SESSION SEMANTICS->session_llseek returned an error: %d\n", -EINVAL);
                return -EINVAL;
        }

        /*
         * Acquire the lock over the session object
         */

        spin_lock(&session->lock);

        /*
         * Get the value of the file pointer for the current session
         */

        file_pointer = session->position;

        /*
         * Set the new value for the file pointer depending on the provided flag for
         * the "origin" parameter
         */

        switch (origin) {
                case SEEK_END: {

                        /*
                         * Return -EINVAL if the new requested position for the file pointer is beyond the actual
                         * limits of the opened file; also release lock on session object
                         */

                        if ((offset > 0) || (file_pointer + offset < 0)) {
                                printk(KERN_INFO "SESSION SEMANTICS->session_llseek returned an error: %d\n", -EINVAL);
                                spin_unlock(&session->lock);
                                return -EINVAL;
                        }

                        /*
                         * File pointer now points to "offset" bytes before the end of the session buffer
                         */

                        session->position = session->filesize + offset;
                        break;
                }
                case SEEK_CUR: {

                        /*
                         * Return -EINVAL if the new requested position for the file pointer is beyond the actual
                         * limits of the opened file; also release lock on session object
                         */

                        if ((file_pointer + offset >= session->filesize) || (file_pointer + offset < 0)) {
                                printk(KERN_INFO "SESSION SEMANTICS->session_llseek returned an error: %d\n", -EINVAL);
                                spin_unlock(&session->lock);
                                return -EINVAL;
                        }

                        /*
                         * File pointer is moved by "offset" places
                         */

                        session->position += offset;
                        break;
                }
                case SEEK_SET: {

                        /*
                         * Return -EINVAL if the new requested position for the file pointer is beyond the actual
                         * limits of the opened file; also release lock on session object
                         */

                        if ((offset < 0) || (file_pointer + offset >= session->filesize)) {
                                printk(KERN_INFO "SESSION SEMANTICS->session_llseek returned an error: %d\n", -EINVAL);
                                spin_unlock(&session->lock);
                                return -EINVAL;
                        }

                        /*
                         * File pointer now points to "offset" bytes from the beginning
                         * of the session buffer
                         */

                        session->position = offset;
                        break;
                }
        }

        /*
         * Relase the lock over the session object
         */

        spin_unlock(&session->lock);

        /*
         * Return the new value of the file pointer
         */

        return session->position;
}

/*
 * According to the session semantics, when an opened file has to
 * be closed, all the modifications made to it using the session
 * buffer have to be transferred to the original copy of the file
 * itself.
 *
 * In order to do that, we set the function pointer to the "flush"
 * operation: in fact, this is invoked by the regular system call
 * "close" so it can be exploited to flush the content of the buffer
 *
 * Since we have to write the content of the buffer into the device
 * the original file belongs to, we need the original "write" file
 * operation (file-system dependent) we stored into the session object
 * when the session was created.
 *
 * Anyway, first we have to truncate the original file to 0 length
 * in order to be compliant with the session semantics: in fact this is
 * the only way to ensure that modifications made to the original file
 * while it was opened in the session are made visible to the other
 * processes when the session is over
 *
 * @file: pointer to struct file of the opened file whose session has to
 * be flushed
 * id: pointer to struct files_struct of the opened file; we ignore this
 * parameter
 *
 * Returns 0 in case of success, -EINVAL is the the session object is not
 * valid and -EIO in case the whole buffer can't be flushed to the original
 * file
 */

int session_close(struct file *file, fl_owner_t id) {

        /*
         * Object representing the current session
         */

        struct session *session;

        /*
         * Virtual address corresponding to the current offset of the opened
         * file
         */

        void *src;

        /*
         * Return value
         */

        int ret;

        /*
         * Memory segment of the process
         */

        mm_segment_t segment;

        /*
         * Get the session object from the opened file
         */

        session = file->private_data;

        /*
         * Return -EINVAL if the session object is not set
         */

        if (!session) {
                printk(KERN_INFO "SESSION SEMANTICS->session_close returned an error: %d\n", -EINVAL);
                return -EINVAL;
        }

        /*
         * Set the return value to 0, indicating success: this is the value
         * returned in case no I/O session has to be flushed, otherwise the
         * return value may change
         */

        ret = 0;

        /*
         * Acquire the lock over the session object: in this way we can be sure
         * that any other conflicting session operation is finished
         */

        spin_lock(&session->lock);

        /*
         * Check the dirty flag of the session object: if dirty, modifications
         * have to be wrtitten into the original file
         */

        if (session->dirty) {

                /*
                 * Current file pointer in the session buffer
                 */

                loff_t off;

                /*
                 * Pointer to the session buffer
                 */

                void *buffer;

                /*
                 * Bytes to be flushed into the original file
                 */

                size_t filesize;

                /*
                 * Truncate the file to 0 length before flushing content
                 */

                ret = truncate_call(session->filename, 0);
                printk(KERN_INFO "TRUNCATED!!!\n");

                /*
                 * Initialize parameters for the write operation from the session
                 * object and the struct file of the opened file
                 */

                buffer = session->buffer;
                filesize = session->filesize;
                off = file->f_pos;

                /*
                 * Mark the kernel space buffer (from which we want to copy bytes to
                 * the original file) as safe one
                 */

                segment = get_fs();
                set_fs(KERNEL_DS);

                /*
                 * Write content to the file using the original write operation
                 * of the opened file
                 */

                printk("params-> buf:%lu filesize:%d off:%d", buffer, filesize, off);
                ret = session->write(file, buffer, filesize, &off);

                /*
                 * Restore memory segment
                 */

                set_fs(segment);

                /*
                 * If the number of bytes flushed to the original file is less
                 * than the size of the buffer, return -EIO (I/O errror)
                 */

                if (ret < session->filesize)
                        ret = -EIO;
        }

        /*
         * Unlock the session
         */

        spin_unlock(&session->lock);

        /*
         * Free the session object
         */

        kfree(session);

        /*
         * Return outcome of the function
         */

        printk(KERN_INFO "SESSION SEMANTICS->session_close returned value: %d\n", ret);
        return ret;
}

/*
 * FILE OPERATIONS IN THE SESSION SEMANTICS - end
 */

/*
 * SESSION INIT - start
 *
 * Set the buffer used by the session to the pages filled with the content of the
 * file and initialize the spinlock in order to synchronize access to the buffer
 * itself
 *
 * @session: session object to be initialized
 * @buffer: virtual address of the contiguous address space to be used as buffer
 * of the session
 * @filename: user-space filename of the opened file
 * @order; number of pages allocated to the buffer
 */

void session_init(struct session *session, void *buffer, const char __user *filename, int order) {


        /*
         * Initialize the spinlock
         */

        spin_lock_init(&session->lock);

        /*
         * Set the buffer
         */

        session->buffer = buffer;

        /*
         * Set the file pointer to 0
         */

        session->position = 0;

        /*
         * Set the dirty flag to false
         */

        session->dirty = false;

        /*
         * Set the filename
         */

        session->filename = filename;

        /*
         * Set the limit of the offset
         */

        session->limit = (1 << order) * PAGE_SIZE;
        printk(KERN_INFO "counter:%d\n",session->limit);

        /*
         * Store the order
         */

        session->order = order;

        /*
         * Initialize the link to the list of sessions
         */

        INIT_LIST_HEAD(&(session->link_to_list));
}


/*
 * SESSION INIT - end
 */

/*
 * SESSION OPERATIONS INSTALL - start
 *
 * Define a new structure for file operations, inherit all the function
 * pointers from the given file except for the read, write and llseek
 * operations, which are instead initialized to their version specific
 * for the session semantics. Former function pointers have to be saved
 * because they have to be used to flush the content of the buffer into
 * the original file when the session is closed.
 *
 * @file: pointer to file struct of the file opened adopting a session semantics
 * @session: pointer to the current session object
 */

void install_session_operations(struct file *file, struct session *session) {

        /*
         * Allocate memory for the new file operations structure
         */

        f_ops = kmalloc(sizeof(struct file_operations), GFP_KERNEL);

        /*
         * Inherit file operations
         */

        *f_ops = *file->f_op;

        /*
         * Save function pointers for original "write"
         * into the session object
         */

        session->write = f_ops->write;
        /*
         * Change pointer for read,write, llseek and flush
         */

        f_ops->read = session_read;
        f_ops->write = session_write;
        f_ops->llseek = session_llseek;
        //f_ops->flush=session_close;

        /*
         * Install the new structure for file operations
         */

        file->f_op = (const struct file_operations *) f_ops;
}

/*
 * SESSION OPERATIONS INSTALL - end
 */

/*
 * SESSION INSTALL - start
 *
 * Replace the basic file operations (read,write and llseek) with their
 * "session equivalent" and initialize the field "private_data" to the
 * address of the session object.
 *
 * The structure with pointer to file operations is costant, so we can
 * not modify it, but rather we have to define a new one with the same
 * function pointers except for read,write and llseek
 *
 * @file: pointer to the struct file of the file involved in the session
 * @session: pointer to the session object
 */

void session_install(struct file *file, struct session *session) {

        /*
         * Install the new functions
         */

        install_session_operations(file, session);

        /*
         * Install the session object into the opened file
         */

        file->private_data = session;

        /*
         * Connect the session object to the list of all the
         * opened sessions
         */
        list_add(&session->link_to_list, &sessions_list->sessions_head);
}

/*
* SESSION INSTALL - end
*/

/*
 * CLEANUP MODULE FOR SESSION SEMANTICS - start
 *
 * Flush the content of all the open sessions, then remove
 * corresponding data structures and free allocated memory
 */

void cleanup_sessions(void) {

        /*
         * Pointer to the session object used during iteration
         */

        struct session *session;

        /*
        * Temporary pointer used inside "list_for_each_entry_safe"
        */

        struct session *temp;

        /*
         * Check if there is an active session
         */

        /*
         * Iterate through the list of open sessions and release
         * their buffers
         */

        list_for_each_entry(session, &sessions_list->sessions_head, link_to_list) {
                remove_session(session);
        }

        /*
         * Remove the session list
         */

        printk(KERN_INFO "ok so far2\n");
        kfree(sessions_list);

        printk(KERN_INFO "done with cleanup sessions\n");
}


/*
 * CLEANUP MODULE FOR SESSION SEMANTICS - end
 */

/*
 * SYS_OPEN WITH SESSION SEMANTICS SUPPORT
 *
 * This is the system call we replace the tradition "open" system call with
 * in order to support the session semantics
 *
 * The new semantics can be required making the bitwise-OR of the flag parameter
 * with the flag "SESSION_OPEN"
 *
 * In case the flag for the session semantics is not specified, the system call
 * opens the given file as usual and returns its file descriptor.
 * If the session semantics is requested, after the file has been opened using
 * the original system call "open", the content of the opened file is copied into
 * some new pages dynamically allocated bypassing the BUFFER CACHE.
 * In this way the current process has its own copy of the file and can freely
 * modify it in such a way that modifications are not visible to other processes
 * that use the same file concurrently.
 *
 * @filename: name of the file to be opened
 * @flags: flags to be used to determine the semantic of the file
 * @mode: permissions of the current process w.r.t. the opened file (mandatory if
 * O_CREAT flag is given)
 *
 * Returns the file descriptor for the opened file or an error code is the operation
 * fails
 */


asmlinkage long sys_session_open(const char __user *filename, int flags, int mode) {

        /*
         * File descriptor of the opened file
         */

        int fd;

        /*
         * Return value
         */

        int ret;

        /*
         * Open file using the original sys_open system call ignoring the flag
         * used to request the session semantics (for the time being)
         */

        if (flags & SESSION_OPEN) {
                fd = previous_open(filename, flags & ~SESSION_OPEN, mode);
                printk(KERN_INFO "SESSION SEMANTICS:Flags for filename %s: %d; file descriptor:%d\n", filename,
                       flags & ~SESSION_OPEN, fd);
        }
        else {
                fd = previous_open(filename, flags, mode);
        }

        /*
         * If this a session open, there's some work to do, otherwise we are done
         * and we return the file descriptor of the opened file
         */

        if (flags & SESSION_OPEN && fd > 0) {

                /*
                 * This structure contains pointers to the functions used by the VFS layer
                 * in order to ask the I/O block layer to trasnfer data to and from devices
                 */

                struct address_space *mapping;

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
                 * Pointer to the page descriptor of the allocated pages; this is
                 * used in the iteration to initialize the session buffer
                 */

                struct page *page;

                /*
                 * Pointer to object used to manage the operations on a file opened
                 * using a session semantics
                 */

                struct session *session;

                /*
                 * Virtual address of the buffer into which the file is stored while
                 * a session is open
                 */

                void *va;

                /*
                 * Structure aimed at keeping track of the virtual address of a page
                 * in the session buffer
                 */

                //struct buffer_page* buffer_page;

                /*
                 * Order of pages to be requested to the function "alloc_pages" in order
                 * to store the content of the opened file
                 */

                int order;

                /*
                 * Index used to iterate through allocated pages
                 */

                int i;

                /*
                 * Size of the file to be opened in bytes;
                 */

                loff_t filesize;

                printk(KERN_INFO "Opening file %s with session semantics\n", filename);

                /*
                 * Get the open file table of the current process
                 */

                files = current->files;

                /*
                 * Get exclusive access to the open file table of the
                 * current process: this is necessary in order to avoid
                 * race conditions in case a file gets closed in the
                 * meantime
                 */

                //spin_lock(&files->file_lock);

                /*
                 * Get the table of file descriptors
                 */

                table = files_fdtable(files);

                /*
                 * Get the file object corresponding to the opened file
                 */

                opened_file = table->fd[fd];

                /*
                 * Get the size of the opened file
                 */

                filesize = opened_file->f_dentry->d_inode->i_size;

                /*
                 * Check the size of the file: if it's zero there's nothing
                 */

                if (!filesize) {
                        printk(KERN_INFO "SESSION SEMANTICS: File %s has size 0\n");
                        return fd;
                }

                /*
                 * Get the order of pages to be allocated using alloc_pages
                 */

                order = get_order(filesize);
                printk(KERN_INFO "order:%d\n", order);

                /*
                 * Get 2^order free pages to store the content of the opened file
                 */

                first_page = alloc_pages(GFP_KERNEL, order);

                /*
                 * Check that the pages have been successfully allocated:
                 * return -ENOMEM in case not enough memory is available
                 * for them
                 */

                if (!first_page) {
                        ret = -ENOMEM;
                        printk(KERN_INFO "System call sys_session_open returned this value:%d\n", ret);
                        return ret;
                }

                /*
                 * Pages are mapped in th virtual address space one after the other
                 * so we only need the virtual address of the first page
                 */

                va = kmap(first_page);

                /*
                 * Allocate a new session object
                 */

                session = kmalloc(sizeof(struct session), GFP_KERNEL);

                /*
                 * Check that the session object has  been successfully
                 * allocated: return -ENOMEM in case not enough memory
                 * is available
                 */

                if (!session) {
                        ret = -ENOMEM;
                        printk(KERN_INFO "System call sys_session_open returned this value:%d\n", ret);
                        return ret;
                }

                /*
                 * Initialize the session object
                 */

                session_init(session, va, filename, order);

                /*
                 * Store the address of the page descriptor of the first page in the buffer:
                 * this will be used when the session has to be removed
                 */

                session->pages = first_page;

                /*
                 * Set the file length in the session object
                 */

                session->filesize = filesize;

                /*
                 * Get the address_space structure of the opened file
                 */

                mapping = opened_file->f_mapping;

                /*
                 * SET THE BUFFER FOR THIS SESSION - start
                 *
                 * Copy the content of the opened file into the session buffer, page by page,
                 * until a number of bytes equal to the filesize has been transferred. At each
                 * the function "readpage", from the address_space of the file, is used to
                 * transfer data from the device where the file is stored to the page frame
                 */

                page = first_page;
                for (i = 0; i < (1 << order); i++) {

                        printk(KERN_INFO "kmap page:%lu\n", page);

                        /*
                         * Lock the page before accessing it
                         */

                        __set_page_locked(page);

                        /*
                         * Initialize the address_space structure of the new
                         * page to the one of the opened file; also set its
                         * index field, which represents the offset of the page
                         * with respect to the beginning of the file (in terms
                         * of pages)
                         */

                        page->mapping = mapping;
                        page->index = i;

                        /*
                         * Copy the content of the file to the newly allocated pages
                         * using the readpage: this is a low-level function which wraps
                         * the function provided by the filesystem to read the content
                         * of its files into memory
                         *
                         * This function creates an instance of the "struct bio" which
                         * represents an I/O request to a block device (like an hard disk)
                         * and submits this request to the controller of the device
                         *
                         * The function returns 0 when the request is successfully submitted:
                         * if this it not the case, we return the error code -EIO (I/O error)
                         * and release the allocated pages, although this should be unlikely.
                         * Also release the lock on the open file table of the current process
                         */

                        ret = mapping->a_ops->readpage(opened_file, page);
                        if (ret) {
                                __free_pages(first_page, order);
                                //spin_unlock(&files->file_lock);
                                printk(KERN_INFO "System call sys_session_open returned this value:%d\n", ret);
                                ret = -EIO;
                                return ret;
                        }

                        /*
                         * When the I/O request to the device has been successfully completed,
                         * the bit "PG_uptodate" in the flag of the page descriptor is set.
                         * Also, when the I/O request has been completed, the PG_locked bit in
                         * the flag of the page is cleared => in order to be sure that our I/O
                         * has been completed, the process goes to sleep using the value of the
                         * bit as condition => the process is woken up when the page gets unlocked.
                         * We use the function "lock_page_killable" to implement this mechanism
                         */

                        if (!PageUptodate(page)) {
                                ret = lock_page_killable(page);

                                /*
                                 * When the process is woken up we check the response of the I/O
                                 * request: in case of error release the pages and return -EIO
                                 * Also release the lock on the open file table of the current
                                 * process
                                 */

                                if (ret) {
                                        __free_pages(first_page, order);
                                        //spin_unlock(&files->file_lock);
                                        printk(KERN_INFO "System call sys_session_open returned this value:%d\n", ret);
                                        ret = -EIO;
                                        return ret;
                                }

                                /*
                                 * Unlock the page and wake up other processes waiting to access
                                 * the page (if any)
                                 */

                                unlock_page(page);
                        }

                        /*
                         * Release the lock on the open file table of the current process
                         */

                        //spin_unlock(&files->file_lock);

                        /*
                         * Go to next page
                         */

                        page += 1;
                }

                /*
                 * SET THE BUFFER FOR THIS SESSION - end
                 */


                 /*
                  * Install the session in the opened file
                  */

                session_install(opened_file, session);
                printk(KERN_INFO "System call sys_session_open returned this value:%d\n", fd);
        }

        /*
         * Return the descriptor of the opened file or an error code
         * in case it opening failed
         */

        return fd;
}
