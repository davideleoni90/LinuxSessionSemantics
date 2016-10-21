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
#include <linux/fcntl.h>
#include "session.h"

extern asmlinkage long (*truncate_call)(const char *path, long length);

/*
 * REMOVE SESSION - start
 *
 * Free the buffer associated to the session, restore the original file
 * operations in the file opened and free the ession object itself.
 *
 * THIS HAS TO BE CALLED HOLDING THE MUTEX ON THE SESSION OBJECT; THE
 * MUTEX IS RELEASED WITHIN THIS FUNCTION
 */

void session_remove(struct session *session) {

        /*
         * Index to scan through the page descriptors
         */

        int i = 0;

        /*
         * Set the "mapping" field of the pages of the buffer
         * to NULL, otherwise "free_pages" complains because
         * pages are still in used
         */

        for (i = 0; i < (1 << (session->order)); i++) {

                /*
                 * Current page descriptor
                 */

                struct page *page;

                /*
                 * Get page descriptor of current page
                 */

                page = session->pages + i;

                /*
                 * Set mapping to NULL for the current page
                 */

                page->mapping = NULL;
        }

        /*
         * Release buffer
         */

        free_pages(session->buffer, session->order);

        /*
         * Restore original file operations in the opened file
         */

        session->file->f_op=session->f_ops_old;

        /*
         * Restore the "private_data" field of the opened file
         */

        session->file->private_data=session->private;

        /*
         * Release the structure with session file operations
         */

        kfree(session->f_ops_new);


        /*
         * Remove the session object from the global list of sessions
         */

        list_del(&session->link_to_list);

        /*
         * Release the mutex on the session object
         */

        mutex_unlock(&session->mutex);

        /*
         * Remove the session object itself
         */

        printk(KERN_INFO "SESSION SEMANTICS->session for file \"%s\" is over\n",session->file->f_dentry->d_name.name);
        kfree(session);
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

void sessions_list_init(struct sessions_list *sessions) {

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
 * associated to the opened file, a mutex has to be acquired first
 *
 * @file: pointer to the file object to be read
 * @buf: user-space buffer where the read content has to be placed
 * @size: number of bytes to read from the file
 * @offset: starting point of the read operation w.r.t to the beginning of the file;
 * this parameter corresponds to the file pointer used in usual I/O operations, so
 * we ignore it
 *
 * Returns number of bytes copied to given buffer, -EINVAL in case the file does
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
         * Acquire the exclusive access over the session object
         */

        mutex_lock(&session->mutex);

        /*
         * Check if the file is empty: is so, just return 0 and release
         * mutex
         */

        if(!session->filesize){
                printk(KERN_INFO "SESSION SEMANTICS->session_read read %d bytes because file is empty\n", 0);
                mutex_unlock(&session->mutex);
                return 0;
        }

        /*
         * Get the value of the file pointer for the current session
         */

        file_pointer = session->position;

        /*
         * Get the position within the file corresponding to the file pointer
         */

        src = session->buffer + file_pointer * sizeof(void);

        /*
         * If the number of bytes requested is beyond the limit of the file,
         * shrink the "size" parameter to a maximum possible value
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

                        left_to_read = left_to_read - ret;
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

        /*
         * Release the mutex over the session object
         */

        mutex_unlock(&session->mutex);

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
 * associated to the opened file, a mutex has to be acquired first
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
         * Acquire the mutex over the session object
         */

        mutex_lock(&session->mutex);

        /*
         * Get the value of the file pointer for the current session
         */

        file_pointer = session->position;

        /*
         * Get the limit of the allocated buffer for the current session
         */

        limit=session->limit;

        /*
         * Get the position within the file corresponding to the file pointer
         */

        dest = session->buffer + file_pointer * sizeof(void);

        /*
         * Check if the write is beyond the limit of the buffer: if so shrink
         * the amount of bytes to write to a suitable size
         */

        if (file_pointer + size >= limit) {

                size = limit - file_pointer;
        }

        if(!size){
                ret=-EINVAL;
                printk(KERN_INFO "SESSION SEMANTICS->session_write wrote returned error %d\nReached limit of the buffer\n", ret);
                return ret;
        }

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

                        left_to_write = left_to_write - ret;
                } while (left_to_write >= 4095);

                /*
                 * Check if the whole file was written
                 */

                if (left_to_write) {

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

        session->position+=(loff_t)ret;

        /*
         * If the file pointer now points to an offset greater or equal than
         * the offset stored in the "filesize" field (recall that offset of
         * file starts from zero, so if file pointer is equal to filesize it
         * means the file changed its size
         * so we have to update the "filesize" field
         */

        if (session->position > session->filesize){
                session->filesize = session->position;
        }

        /*
         * Set the "dirty" flag of the session buffer because it has been
         * modified so there are some updates to transfer to the original
         * file
         */

        session->dirty = true;

        /*
         * Release the exclusive lock over the session object
         */

        mutex_unlock(&session->mutex);

        /*
         * Return the number of bytes copied
         */

        return ret;
}

/*
 * According to the session semantics, seeking a file means simply changing the field "position"
 * inside the session object, namely moving the session file pointer accordinf to the
 * mode specified as parameter
 *
 * In order to avoid race conditions among processes sharing the same struct file
 * associated to the opened file, a mutex has to be acquired first
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
         * Acquire the mutex over the session object
         */

        mutex_lock(&session->mutex);

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
                         * limits of the opened file; also release mutex on session object
                         */

                        if ((offset > 0) || (offset <= -(session->filesize))) {
                                printk(KERN_INFO "SESSION SEMANTICS->session_llseek returned an error: %d\n", -EINVAL);
                                mutex_unlock(&session->mutex);
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
                         * limits of the opened file; also release mutex on session object
                         */

                        if ((file_pointer + offset >= session->filesize) || (file_pointer + offset < 0)) {
                                printk(KERN_INFO "SESSION SEMANTICS->session_llseek returned an error: %d\n", -EINVAL);
                                mutex_unlock(&session->mutex);
                                return -EINVAL;
                        }

                        /*
                         * File pointer is moved by "offset" places
                         */

                        session->position += offset;
                        printk(KERN_INFO "lseek:%d char:%c\n",session->position,((char*)(session->buffer))[session->position]);
                        break;
                }
                case SEEK_SET: {

                        /*
                         * Return -EINVAL if the new requested position for the file pointer is beyond the actual
                         * limits of the opened file; also release mutex on session object
                         */

                        printk("filesize:%d\n",session->filesize);
                        if ((offset < 0) || (offset >= session->filesize)) {
                                printk(KERN_INFO "SESSION SEMANTICS->session_llseek returned an error: %d\n", -EINVAL);
                                mutex_unlock(&session->mutex);
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
         * Release the mutex over the session object
         */

        mutex_unlock(&session->mutex);

        /*
         * Return the new value of the file pointer
         */

        printk(KERN_INFO "SESSION SEMANTICS->session_llseek set new position to: %d\n", session->position);
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
 * processes when the session is over.
 * The system call "sys_truncate" is used to truncate the file
 *
 * @file: pointer to struct file of the opened file whose session has to
 * be flushed
 * id: pointer to struct files_struct of the opened file; we ignore this
 * parameter
 *
 * Returns 0 in case of success, -EINVAL is the the session object is not
 * valid and -EIO in case the whole buffer can't be flushed to the original
 * file. Moreover, in case the system call "sys_truncate" fails, its error
 * code is returned.
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
         * Acquire the mutex over the session object: in this way we can be sure
         * that any other conflicting session operation is finished
         */

        mutex_lock(&session->mutex);

        /*
         * Check the dirty flag of the session object: if dirty, modifications
         * have to be wrtitten into the original file
         */

        if (session->dirty) {

                /*
                 * Current file pointer in the session buffer
                 */

                loff_t off=0;

                /*
                 * Pointer to the session buffer
                 */

                void *buffer;

                /*
                 * Bytes to be flushed into the original file
                 */

                size_t filesize;

                /*
                 * Since we are now going to invoke two system calls
                 * (write and truncate) that expect a buffer from the
                 * user space, we first have to mark the kernel space
                 * (where is actually the buffer given to them) as safe
                 */

                segment = get_fs();
                set_fs(KERNEL_DS);

                /*
                 * Truncate file to zero length before flushing the content
                 */

                ret = truncate_call(session->filename, 0);

                /*
                 * Return error code if truncate fails; before the
                 * session has to be unlocked
                 */

                if(!ret) {
                        set_fs(segment);
                        mutex_unlock(&session->mutex);
                        printk(KERN_INFO "SESSION SEMANTICS->session_close returned error: %d\n", ret);
                }

                /*
                 * Initialize parameters for the write operation from the session
                 * object and the struct file of the opened file
                 */

                buffer = session->buffer;
                filesize = session->filesize;
                off = file->f_pos;

                /*
                 * Write content to the file using the original write operation
                 * of the opened file: the number of written values is returned.
                 * First the memory segment has to be set.
                 */

                set_fs(KERNEL_DS);
                ret = session->f_ops_old->write(file, buffer, filesize, &off);

                /*
                 * Restore memory segment
                 */

                set_fs(segment);

                /*
                 * If the number of bytes flushed to the original file is less
                 * than the size of the buffer, return -EIO (I/O error)
                 */

                if (ret < session->filesize)
                        ret = -EIO;
        }

        /*
         * Unlock the session
         */

        //mutex_unlock(&session->mutex);

        /*
         * Remove the session object and its associated data structures
         */

        session_remove(session);

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
 * file and initialize its mutex semaphore
 *
 * @session: session object to be initialized
 * @buffer: virtual address of the contiguous address space to be used as buffer
 * of the session
 * @filename: user-space filename of the opened file
 * @order; number of pages allocated to the buffer
 */

void session_init(struct session *session, void *buffer, const char __user *filename, int order) {

        /*
         * Length of the string representing the filename
         */

        int length;

        /*
         * Initialize the mutex
         */

        mutex_init(&session->mutex);

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
         * Get the length of the string representing the filename
         */

        length=strlen(filename);

        /*
         * Dynamically allocate a string to store the filename
         */

        session->filename=kmalloc(length,GFP_KERNEL);

        /*
         * Store the filename into the session
         */

        copy_from_user(session->filename,filename,length);

        /*
         * Set the limit of the offset
         */

        session->limit = (1 << order) * PAGE_SIZE;

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
 *
 * Returns 0 in case of success, -ENOMEM in case not enough memory is available
 * to allocate the new file operations
 */

int session_install_operations(struct file *file, struct session *session) {

        /*
         * Pointer to new structure for file operations
         */

        struct file_operations *f_ops;

        /*
         * Save function pointers to the original file operations into the
         * session object: they will be restored when the session is over
         */

        session->f_ops_old = file->f_op;

        /*
         * Allocate memory for the new file operations structure
         */

        f_ops = kmalloc(sizeof(struct file_operations), GFP_KERNEL);

        /*
         * Check if memory is available to allocate the new file operations
         */

        if(!f_ops)
                return -ENOMEM;

        /*
         * Inherit file operations
         */

        *f_ops = *file->f_op;

        /*
         * Change pointer for read,write, llseek and flush
         */

        f_ops->read = session_read;
        f_ops->write = session_write;
        f_ops->llseek = session_llseek;
        f_ops->flush=session_close;

        /*
         * Install the new structure for file operations
         */

        file->f_op = (const struct file_operations *) f_ops;

        /*
         * Store the pointer to the new structure in the session
         * object in order to easily restore original operations
         * when the session is over and also in order to release
         * allocates memory
         */

        session->f_ops_new=f_ops;

        /*
         * File operations successfully installed: return 0
         */

        return 0;

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
 *
 * Returns 0 in case of success, -ENOMEM in case allocation of data
 * structures fails
 */

int session_install(struct file *file, struct session *session) {


        /*
         * Return value
         */

        int ret;

        /*
         * Install the new functions
         */

        ret=session_install_operations(file, session);

        /*
         * Check if new file operations have been properly
         * installed: if not, return error code
         */

        if(ret)
                return ret;

        /*
         * Install the session object into the opened file
         */

        file->private_data = session;

        /*
         * Store the pointer to the structure "file" into the
         * session object
         */

        session->file=file;

        /*
         * Connect the session object to the list of all the
         * opened sessions
         */

        list_add(&session->link_to_list, &sessions_list->sessions_head);

        /*
         * New session has been successfully installed: return 0
         */

        return 0;
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

void sessions_remove(void) {

        /*
         * Pointer to the session object used during iteration
         */

        struct session* session;

        /*
        * Temporary pointer used inside "list_for_each_entry_safe"
        */

        struct session *temp;

        /*
         * Iterate through the list of open sessions and release
         * their buffers; for each session object, acquire its
         * mutex before
         */

        list_for_each_entry_safe(session,temp,&sessions_list->sessions_head,link_to_list) {
                mutex_lock(&session->mutex);
                session_remove(session);
        }

        /*
         * Remove the session list
         */

        if(sessions_list) {
                kfree(sessions_list);
        }
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
                printk(KERN_INFO "SESSION SEMANTICS:Flags for filename \"%s\": %d; file descriptor:%d\n", filename,
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
                 * Pointer to the descriptor of the first page in the buffer allocated to
                 * read the file
                 */

                struct page *first_page;

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
                 * Get the size of the opened file
                 */

                filesize = opened_file->f_dentry->d_inode->i_size;

                /*
                 * Check the size of the file: if it's bigger than zero, its content is copied
                 * into a buffer of dynamically allocated pages and then the session object is
                 * created; if it's zero, only the session object is created
                 */

                if (!filesize) {

                        /*
                         * File is empty, allocate only four pages in case new content has
                         * to be added; order is 2
                         */

                        printk(KERN_INFO "SESSION SEMANTICS-> File \"%s\" has size 0\n",filename);
                        first_page = alloc_pages(GFP_KERNEL, 2);
                        order=0;
                }
                else{

                        /*
                         * File is not empty, so allocate as many pages as
                         * necessary to store the file
                         */

                        /*
                         * Get the order of pages to be allocated using alloc_pages
                         */

                        order = get_order(filesize);

                        /*
                         * Get 2^order free pages to store the content of the opened file
                         * and add an equal number of pages in case new content has to be
                         * added
                         */

                        first_page = alloc_pages(GFP_KERNEL, order+1);
                }

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
                 * Pages are mapped in the virtual address space one after the other
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
                 * Save private data of the opened file (if any)
                 */

                if(opened_file->private_data) {
                        session->private=opened_file->private_data;
                }

                /*
                 * If the file is not empty, copy it content into the allocated
                 * session buffer
                 */

                if(!filesize) {

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
                                                printk(KERN_INFO "System call sys_session_open returned this value:%d\n",
                                                       ret);
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
                                 * Go to next page
                                 */

                                page += 1;
                        }

                        /*
                         * SET THE BUFFER FOR THIS SESSION - end
                         */

                }

                /*
                 * Install the session in the opened file
                 */

                ret=session_install(opened_file, session);

                /*
                 * Check if the session has been properly installed:
                 * if not, return corresponding error code
                 */

                if(ret)
                        return ret;

                /*
                 * The session has been successfully opened: return file descriptor
                 * of the file that is now opened adopting the session semantics
                 */

                printk(KERN_INFO "System call sys_session_open returned this value:%d\n", fd);
        }

        /*
         * Return the descriptor of the opened file or an error code
         * in case it opening failed
         */

        return fd;
}
