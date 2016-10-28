#include <linux/fs.h>
#include <linux/gfp.h>
#include <asm-generic/current.h>
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
#include <linux/backing-dev.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include "session.h"

extern asmlinkage long (*truncate_call)(const char *path, long length);
extern struct file* get_file_from_descriptor(int fd);

/*
 * NEW BUFFER PAGE - start
 *
 * Create a new object of type "buffer_page" to keep track of pages of
 * the session buffer
 *
 * @buffer_page_descriptor: pointer to the descriptor associate to the allocated
 * frame
 * @index: index of the page within the buffer
 *
 * Returns a pointer to the new instance of buffer_page if successful, -ENOMEM if
 * not enough memory is available for the creation of the new object and -EINVAL
 * if one of the given pointer is NULL
 */

struct buffer_page* session_new_buffer_page(void* buffer_page_address,struct page* buffer_page_descriptor,int index){

        /*
         * Pointer to the new buffer_page object
         */

        struct buffer_page* buffer_page;

        printk(KERN_INFO "SESSION SEMANTICS->Creating buffer page for address %lu and descriptor %lu, with index %d\n",buffer_page_address,buffer_page_descriptor,index);

        /*
         * Check if provided addresses are valid: if not, return -EFAULT
         */

        if((!buffer_page_address)||(!buffer_page_descriptor))
                return ERR_PTR(-EFAULT);

        /*
         * Check if provided index is valid: if not, return -EINVAL
         */

        if(index<0)
                return ERR_PTR(-EINVAL);

        /*
         * Allocate a new object of type "buffer_page"
         */

        buffer_page=kmalloc(sizeof(struct buffer_page),GFP_KERNEL);

        /*
         * Check if allocation was successful: if not return -ENOMEM
         */

        if(!buffer_page)
                return ERR_PTR(-ENOMEM);

        /*
         * Set the descriptor and address of the buffer page
         */

        buffer_page->buffer_page_address=buffer_page_address;
        buffer_page->buffer_page_descriptor=buffer_page_descriptor;

        /*
         * Set the index of the buffer_page
         */

        buffer_page->index=index;

        /*
         * Initialize the "list_head" field
         */

        INIT_LIST_HEAD(&buffer_page->buffer_pages_head);

        /*
         * Return the pointer to allocated address
         */

        printk(KERN_INFO "SESSION SEMANTICS->Created buffer page %d, corresponding to virtual address %lu\n",buffer_page->index,buffer_page->buffer_page_address);
        return buffer_page;
}

/*
 * NEW BUFFER PAGE - end
 */

/*
 * CREATE SESSION BUFFER - start
 *
 * Allocate a suitable number of pages to store the content of the file in
 * session
 *
 * @filesize: size of the opened file
 * @filename: filename of the file
 * @order: pointer to the order variable, that has to be set to the order of
 * pages allocated
 *
 * Returns the pointer to the descriptor of the first page of the buffer,NULL
 * if not enough memory is available
 */

struct page* session_create_buffer(int filesize,const char __user *filename,int* order){

        /*
         * Pointer to the descriptor of the first page in the buffer allocated to
         * read the file
         */

        struct page *first_page;

        /*
         * Check size of the file
         */

        if (!filesize) {

                /*
                 * File is empty, allocate only one page; order is 0
                 */

                printk(KERN_INFO "SESSION SEMANTICS-> File \"%s\" has size 0\n",filename);
                first_page = alloc_pages(GFP_KERNEL, 0);
                *order=0;
        }
        else{

                /*
                 * File is not empty, so allocate as many pages as
                 * necessary to store the actual size of the file
                 */

                /*
                 * Get the order of pages to be allocated using alloc_pages
                 */

                *order = get_order(filesize);

                /*
                 * Get 2^order free pages to store the content of the opened file
                 */

                first_page = alloc_pages(GFP_KERNEL, *order);
        }

        /*
         * Return pointer to descriptor of the first page
         */

        return first_page;
}

/*
 * CREATE SESSION BUFFER - end
 */

/*
 * FILL SESSION BUFFER - start
 *
 * Copy the content of the opened file into the session buffer, page by page,
 * until a number of bytes equal to the filesize has been transferred. At each
 * the function "readpage", from the address_space of the file, is used to
 * transfer data from the device where the file is stored to the page frame
 *
 * @first_page: pointer to the descriptor of the first page in the buffer
 * @mapping: pointer to "address_space" structure of the file, containing
 * pointers to functions to be used for low level I/O operations related to
 * the file
 * @order: 2^order pages belong to the buffer
 * @opened_file: file structure associated to opened file
 *
 * Returns 0 if the whole file is copied, an error code otherwise
 */

int session_fill_buffer(struct page* first_page,int order,struct file *opened_file){

        /*
         * This structure contains pointers to the functions used by the VFS layer
         * in order to ask the I/O block layer to transfer data to and from devices
         */

        struct address_space *mapping;

        /*
         * Return value
         */

        int ret;

        /*
         * Pointer to the page descriptor of the allocated pages; this is
         * used in the iteration to initialize the session buffer
         */

        struct page *page;

        /*
         * Index used to iterate through allocated pages
         */

        int i;

        /*
         * Get the address_space structure of the opened file
         */

        mapping = opened_file->f_mapping;

        /*
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

                        /*
                         * Return error
                         */

                        printk(KERN_INFO "SESSION SEMANTICS->Filling buffer returned error:%d\n",ret);
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

                                /*
                                 * Return error
                                 */

                                printk(KERN_INFO "SESSION SEMANTICS->Filling buffer returned error:%d\n",ret);
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
         * The content of the file was fully copied from its storage into the session
         * without making use of the buffer, so return 0
         */

        printk(KERN_INFO "SESSION SEMANTICS->Filling buffer was successfull\n");
        return 0;
}

/*
 * FILL SESSION BUFFER - end
 */

/*
 * EXPAND SESSION BUFFER - start
 *
 * Ask the system for the allocation of new pages, map them and add them to
 * the buffer of the session object
 *
 * @session: pointer to the object representing the current session
 * @size: number of additional bytes that don't fit into the actual size of
 * the buffer
 *
 * Returns the number of pages added if succeeds, -ENOMEM if not enough memory
 * is available for the creation of the new object and -EINVAL if the given
 * pointer is NULL
 */

int session_expand_buffer(struct session* session, int size){

        /*
         * 2^(new_order) new pages have to be allocated to
         * the buffer
         */

        int new_order;

        /*
         * Index to iterate through newly allocated pages
         */

        int i;

        /*
         * Virtual address of newly allocated pages of the buffer
         */

        void* new_va;

        /*
         * Pointer to the descriptor of the first newly allocated page
         */

        struct page* new_first;

        /*
         * Check if parameters are valid
         */

        if((!session)||(!size)){

                /*
                 * Return -EINVAL if the session object is not set or size is 0
                 */

                if (!session) {
                        printk(KERN_INFO "SESSION SEMANTICS->session_expand_buffer: session is NULLL\n", -EINVAL);
                        return -EINVAL;
                }
                if (!session) {
                        printk(KERN_INFO "SESSION SEMANTICS->session_expand_buffer was passed 0 size\n");
                        return -EINVAL;
                }
        }

        /*
         * Get the order of pages necessary to store the requested amount of bytes
         */

        new_order=get_order((loff_t)size);

        /*
         * Allocated requested pages
         */

        new_first=alloc_pages(GFP_KERNEL,new_order);

        /*
         * Map the newly allocated pages into the virtual address
         */

        new_va=kmap(new_first);

        /*
         * Create an object "buffer_page" for each newly allocated page and add
         * it to the buffer of the session
         */

        for(i=0;i<(1<<new_order);i++){

                /*
                 * Pointer to the new buffer_page object
                 */

                struct buffer_page* buffer_page;

                /*
                 * Create a new "buffer_page" object for each new page of the buffer
                 */

                buffer_page=session_new_buffer_page(new_va+i*PAGE_SIZE,new_first+i,session->nr_pages+i);

                /*
                 * Check if the creation of the new object was successful: if not,
                 * return the associated error code
                 */

                if(IS_ERR_VALUE(PTR_ERR(buffer_page)))
                        return PTR_ERR(buffer_page);

                /*
                 * Add the newly created object to the corresponding list in the session
                 * object
                 */

                printk(KERN_INFO "SESSION SEMANTICS->Adding buffer page %d to session\n",buffer_page->index);
                list_add_tail(&buffer_page->buffer_pages_head,&(session->pages));
        }

        /*
         * Buffer was successfully expanded, so return number of new pages
         */

        return (1<<new_order);
}

/*
 * EXPAND SESSION BUFFER - end
 */

/*
 * REMOVE SESSION - start
 *
 * Free the buffer associated to the session, restore the original file
 * operations in the file opened and free the session object itself.
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
         * Pointer to barrier_page tpe, used to iterate through
         * the pages in the buffer of the session; "temp" is
         * used in the iteration because we are deleting entries
         * from the list
         */

        struct buffer_page* buffer_page;
        struct buffer_page* temp;

        /*
         * Iterate through the objects of type "buffer_page" stored
         * in the session object and for each of them:
         *
         * 1- set the "mapping" field of the page descriptor to NULL,
         * otherwise "free_page" complains because the descriptor is
         * still in use
         *
         * 2- release frame associated to the page
         *
         * 3- remove page from list of pages in session
         *
         * 4- release the buffer_page object itself
         */

        list_for_each_entry_safe(buffer_page,temp,&session->pages,buffer_pages_head){
                buffer_page->buffer_page_descriptor->mapping=NULL;
                free_page(buffer_page->buffer_page_address);
                list_del(&buffer_page->buffer_pages_head);
                kfree(buffer_page);
        }

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
         * Release memory allocated to store the filename
         */

        kfree(session->filename);

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
 * not contain a reference to the session object and -EIO in case not all bytes
 * requested can be read from session buffer
 *
 */


ssize_t session_read(struct file *file, char __user * buf, size_t size, loff_t *offset) {

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
         * Index of the page in the buffer
         */

        int index;

        /*
         * Pointer to the buffer_page object corresponding to the actual position
         * of the session file pointer
         */

        struct buffer_page* current_page;

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
         * Get the file pointer of the current session
         */

        file_pointer = session->position;

        /*
         * If the number of bytes requested to read is beyond the limit of the file,
         * shrink the "size" parameter to the maximum possible value
         */

        if (file_pointer + size > session->filesize) {
                size = session->filesize - file_pointer;
        }

        /*
         * Get the index of the page in the buffer corresponding to the session file pointer
         */

        index=file_pointer/PAGE_SIZE;

        printk(KERN_INFO "SESSION SEMANTICS->Index of first page to read:%d\n", index);

        /*
         * Get the page of the buffer corresponding to the file pointer
         */

        list_for_each_entry(current_page,&session->pages,buffer_pages_head) {
                if (current_page->index == index) {
                        break;
                }
        }

        printk(KERN_INFO "SESSION SEMANTICS->Index of page corresponding to file pointer:%d\n",
               current_page->index);

        /*
         * Get the exact location within the buffer from which the requested number of bytes
         * will be read
         */

        src = current_page->buffer_page_address + (file_pointer % PAGE_SIZE) * sizeof(void);

        printk(KERN_INFO "SESSION SEMANTICS->Address from which read operation starts:%lu\n",src);

        /*
         * First copy bytes from the page of the buffer corresponding to the actual position
         * of the file pointer.
         *
         * If the number of bytes requested extends beyond the current page, copy all the bytes
         * from the current page, starting from the file pointer, and then copy bytes from next
         * page/s of the buffer, until all requested bytes have been read. Otherwise (i.e. if
         * all the requested bytes are contained in the current page of the buffer) copy "size"
         * bytes to the user-space buffer.
         */

        if((file_pointer % PAGE_SIZE)+size>PAGE_SIZE) {

                /*
                 * Copy first bytes
                 */

                ret=copy_to_user(buf, src, PAGE_SIZE - (file_pointer % PAGE_SIZE));

                /*
                 * Check that all bytes have been copied: if not, return -EIO as error
                 */

                if(ret){
                        printk(KERN_INFO "SESSION SEMANTICS->%d bytes could not be copied in session_read\n",
                               ret);
                        return -EIO;
                }

                printk(KERN_INFO "SESSION SEMANTICS->First bytes copied:%d\n",
                       (PAGE_SIZE - (file_pointer % PAGE_SIZE)));

                /*
                 * Increment pointer in the user-space buffer
                 */

                buf = buf + (PAGE_SIZE - ((file_pointer % PAGE_SIZE))) * sizeof(void);

                /*
                 * Set the number of bytes left to read
                 */

                left_to_read = size - (PAGE_SIZE - ((file_pointer % PAGE_SIZE)));
                printk(KERN_INFO "SESSION SEMANTICS->Bytes left to read:%d\n",left_to_read);

                /*
                 * Copy the content of buffer pages into the user-space buffer starting from
                 * their base address until the remaining bytes have been read
                 */

                do {

                        /*
                         * Bytes copied
                         */

                        int copied;

                        /*
                         * Get current page within the buffer
                         */

                        current_page = list_entry(current_page->buffer_pages_head.next, struct buffer_page,
                                                  buffer_pages_head);
                        printk(KERN_INFO "SESSION SEMANTICS->Index of buffer page read now:%d\n",current_page->index);

                        /*
                         * Check that the pointer does not point to the head of list:
                         * if this happens instead, it means it was not possible to
                         * copy all the requested bytes from the session buffer, then
                         * return -EIO error
                         */

                        if(current_page==&session->pages){

                                /*
                                 * Release the mutex over the session object
                                 */

                                mutex_unlock(&session->mutex);

                                /*
                                 * Return the error code
                                 */

                                printk(KERN_INFO "SESSION SEMANTICS->session_read returned an error: %d\n", -EIO);
                                return -EIO;

                        }

                        /*
                         * In case the number of bytes left to read is bigger than
                         * PAGE_SIZE, copy PAGE_SIZE bytes from the buffer page
                         */

                        if (left_to_read > PAGE_SIZE) {

                                /*
                                 * Copy a page of data
                                 */

                                printk(KERN_INFO "SESSION SEMANTICS->Bytes left to read:%d\n",left_to_read);
                                copied = copy_to_user(buf, current_page->buffer_page_address, PAGE_SIZE);
                                if (copied)
                                        printk(KERN_INFO "SESSION SEMANTICS->%d bytes could not be copied  in session_read\n",
                                               copied);

                                /*
                                 * Update positions in the user-space buffer
                                 */

                                buf = buf + (PAGE_SIZE-copied)*sizeof(void);

                                /*
                                 * Update counter of bytes left to read
                                 */

                                left_to_read = left_to_read - (PAGE_SIZE-copied);
                        }
                        else {

                                /*
                                 * Copy less than a page of data
                                 */
                                printk(KERN_INFO "SESSION SEMANTICS->Bytes left to read:%d\n",left_to_read);
                                copied = copy_to_user(buf, current_page->buffer_page_address, left_to_read);
                                if (copied)
                                        printk(KERN_INFO "SESSION SEMANTICS->%d bytes could not be copied  in session_read\n",
                                               copied);
                                /*
                                 * Update positions in the user-space buffer
                                 */

                                buf = buf + (left_to_read-copied) * sizeof(void);

                                /*
                                 * If last copy was successfull, "copied" is 0 and we are done
                                 * with reading
                                 */

                                left_to_read = copied;
                        }
                } while (left_to_read);
        }
        else{

                /*
                 * The current page contains all requested bytes: read them
                 */

                if(copy_to_user(buf, src, size)){

                        /*
                         * We get here if some bytes could not be copied:
                         * return -EIO as error code
                         */

                        printk(KERN_INFO "SESSION SEMANTICS->session_read returned an error: %d\n", -EIO);
                        return -EIO;

                }
        }


        /*
         * Move the position of the file pointer in the session object
         * forward by the number of bytes copied from session buffer to
         * user-space buffer
         */

        session->position += (loff_t) size;

        /*
         * Release the mutex over the session object
         */

        mutex_unlock(&session->mutex);

        /*
         * Return the number of bytes copied
         */

        printk(KERN_INFO "SESSION SEMANTICS->session_read read %d bytes\n", size);
        return size;
}

/*
 * According to the session semantics, writing a file means simply copying the content of
 * the given buffer into the buffer when the opened file is stored; also the
 * file pointer of the session has to be updated.
 *
 * If the the number of bytes requested for the write operation, given the current value
 * for the file pointer, is beyond the allocated buffer, new pages have to be allocated to
 * the buffer itself and their addresses have to be stored in the session object.
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
 * Returns number of bytes written into the buffer associated to the opened file,
 * -EINVAL in case the file does not contain a reference to the session object and
 * -EIO in case not all bytes requested can be read from session buffer
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
         * Index of the page in the buffer
         */

        int index;

        /*
         * Pointer to the buffer_page structure associated to the current page
         * within the buffer
         */

        struct buffer_page* current_page;

        /*
         * Bytes left to write in the loop in case the size is larger than PAGE_SIZE
         */

        int left_to_write;

        /*
         * Return value;
         */

        ssize_t ret;

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
         * Check if the number of bytes to write is positive: if not, return 0
         */

        if(!size){
                printk(KERN_INFO "WARNING: SESSION SEMANTICS->session_write: requested 0 bytes to read\n");
                printk(KERN_INFO "SESSION SEMANTICS->session_write wrote %d bytes\n",0);
                return 0;
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
         * Get the index of the page in the buffer corresponding to the session file pointer
         */

        index=file_pointer/PAGE_SIZE;
        printk(KERN_INFO "SESSION SEMANTICS->Index of first page to write:%d\n", index);

        /*
         * Get the page of the buffer corresponding to the file pointer
         */

        list_for_each_entry(current_page,&session->pages,buffer_pages_head) {
                if (current_page->index == index) {
                        break;
                }
        }

        printk(KERN_INFO "SESSION SEMANTICS->Index of page corresponding to file pointer:%d\nBase address:%lu",
               current_page->index,current_page->buffer_page_address);

        /*
         * Get the exact location within the buffer from which the requested number of bytes
         * will be written
         */

        dest = current_page->buffer_page_address + (file_pointer % PAGE_SIZE) * sizeof(void);

        printk(KERN_INFO "SESSION SEMANTICS->Address from which write operation starts:%lu\n", dest);

        /*
         * If the requested size is bigger than the actual size of the session buffer, new pages have
         * to be allocated in order to satisfy the user request. Because of this, writing takes two
         * steps:
         *
         * 1- expand the session buffer allocating new pages in case bytes written go beyond the limit
         * of the session buffer
         *
         * 2- copy bytes from the user-space buffer to the session buffer until all the pages of the
         * latter have been modified
         *
         * As regard with the second step, we adopt the same strategy used for "session_read", i.e the
         * page of the buffer corresponding to the file pointer is filled starting from a possibly
         * non-zero offset, and then the rest of the pages are written starting from their base address
         */

        if((file_pointer % PAGE_SIZE)+size>PAGE_SIZE) {

                /*
                 * Bytes that could not be copied
                 */

                int failed;

                /*
                 * Return value from function to expand the session buffer
                 */

                int expand_buffer;

                /*
                 * Copy first bytes
                 */

                failed=copy_from_user(dest, buf, PAGE_SIZE - ((file_pointer % PAGE_SIZE)));

                /*
                 * Check that all bytes could be copied: if not, return -EIO error
                 */

                if(failed){
                        printk(KERN_INFO "SESSION SEMANTICS->%d bytes could not be copied in session_write\n",
                               ret);
                        return -EIO;
                }

                /*
                 * Copy was successful
                 */

                printk(KERN_INFO "SESSION SEMANTICS->Number of bytes written to fill the page:%d\n",
                       PAGE_SIZE - ((file_pointer % PAGE_SIZE)));

                /*
                 * Increment pointer in the user-space buffer
                 */

                buf = buf + (PAGE_SIZE - ((file_pointer % PAGE_SIZE))) * sizeof(void);

                /*
                 * Set the number of bytes left to write
                 */

                left_to_write = size - (PAGE_SIZE - ((file_pointer % PAGE_SIZE)));

                /*
                 * We filled the page containing the file pointer with first bytes
                 * from the user-space buffer => increment the index variable
                 */

                ++index;

                /*
                 * If the left bytes go beyond the actual boundary of the file, expand
                 * the buffer with a proper number of pages before start copying data
                 * from user-space to session buffer
                 */

                if(left_to_write/PAGE_SIZE+1>session->nr_pages-index) {

                        /*
                         * Expand the buffer
                         */

                        expand_buffer = session_expand_buffer(session, left_to_write);

                        /*
                         * Check the return value: if positive, the buffer has been expanded
                         * successfully and we update the number of pages in the buffer, while
                         * if it's negative something went wrong so we stop and return error
                         * code
                         */

                        if(expand_buffer>=0)
                                session->nr_pages+=expand_buffer;
                        else {
                                printk(KERN_INFO "SESSION SEMANTICS->Could not expand the buffer because of error:%d\n",expand_buffer);
                                mutex_unlock(&session->mutex);
                                return expand_buffer;
                        }
                        printk(KERN_INFO "SESSION SEMANTICS->Expanded buffer: now there are %d pages\n",session->nr_pages);
                }

                /*
                 * Copy the content of buffer pages into the user-space buffer starting from
                 * their base address until the remaining bytes have been read
                 */

                do {

                        /*
                         * Bytes copied
                         */

                        int copied;

                        /*
                         * Get current page within the buffer
                         */

                        current_page = list_entry(current_page->buffer_pages_head.next, struct buffer_page,
                                                  buffer_pages_head);
                        printk(KERN_INFO "SESSION SEMANTICS->Index of current page to write:%d\n", current_page->index);

                        /*
                         * Check that the pointer does not point to the head of list:
                         * if this happens instead, it means that we need more pages
                         * for the session buffer because the size of bytes to be
                         * written goes beyond the limit of the session buffer.
                         *
                         * If this is the case, it means that some pages could not be
                         * copied from the user-space buffer in previous iteration
                         * => return -EIO error code
                         */

                        if(current_page==&session->pages){
                                printk(KERN_INFO "SESSION SEMANTICS->Some bytes could not be copied in session_write\n");
                                return -EIO;
                        }

                        /*
                         * In case the number of bytes left to write is bigger than
                         * PAGE_SIZE, copy PAGE_SIZE bytes into the buffer page
                         */

                        printk(KERN_INFO "SESSION SEMANTICS->Bytes left to write:%d\n", left_to_write);
                        if (left_to_write > PAGE_SIZE) {

                                /*
                                 * Copy a page of data
                                 */

                                copied = copy_from_user(current_page->buffer_page_address,buf, PAGE_SIZE);

                                /*
                                 * Update positions in the user-space buffer
                                 */

                                buf = buf + (PAGE_SIZE-copied) * sizeof(void);

                                /*
                                 * Update counter of bytes left to write
                                 */

                                left_to_write = left_to_write - (PAGE_SIZE-copied);
                        }
                        else {

                                /*
                                 * Copy less than a page of data
                                 */

                                copied = copy_from_user(current_page->buffer_page_address,buf,left_to_write);

                                /*
                                 * Update positions in the user-space buffer
                                 */

                                buf = buf + (left_to_write-copied) * sizeof(void);

                                /*
                                 * If all bytes were successfully written, "copied" is
                                 * equal to 0, so we exit the loop
                                 */

                                left_to_write = copied;
                        }
                } while (left_to_write);

                /*
                 * After expanding the session buffer, all requested bytes have been copied from
                 * user-space buffer into session buffer
                 */
        }
        else{

                /*
                 * The current page suffices to copy all requested bytes
                 */

                ret=copy_from_user(dest, buf, size);

                /*
                 * Check if all bytes could be copied into session buffer: if not,
                 * return -EIO error
                 */

                if(ret){

                        /*
                         * We get here if some bytes could not be copied:
                         * return -EIO
                         */

                        printk(KERN_INFO "SESSION SEMANTICS->Some bytes could not be copied in session_write\n");
                        return -EIO;
                }
        }

        /*
         * Move the position of the file pointer in the session object
         * forward by the number of bytes copied from user-space to
         * session buffer
         */

        session->position+=(loff_t)size;

        /*
         * If the file pointer now points to an offset greater or equal than
         * the offset stored in the "filesize" field (recall that offset of
         * file starts from zero, so if file pointer is equal to filesize it
         * means the file changed its size so we have to update the "filesize"
         * field
         */

        if (session->position > session->filesize){
                session->filesize += (session->position-session->filesize);
                printk(KERN_INFO "SESSION SEMANTICS->session_write increased filesize to:%d\n",session->filesize);
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

        printk(KERN_INFO "SESSION SEMANTICS->session_write wrote %d bytes\n",size);
        return size;
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
 * NOTE: FILE HOLES ARE NOT ALLOWED -> it is possible to set the session file pointer only
 * within the boundaries of actual size of the file
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
         * Index of the page in the buffer
         */

        int index;

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
         * Get the index of the page in the buffer corresponding to the session file pointer
         */

        index=file_pointer/PAGE_SIZE;

        /*
         * Set the new value for the file pointer depending on the provided flag for
         * the "origin" parameter
         */
        printk(KERN_INFO "SESSION SEMANTICS->Current position of session file pointer:%d\n",session->position);
        printk(KERN_INFO "SESSION SEMANTICS->Current filesize:%d\n",session->filesize);
        switch (origin) {
                case SEEK_END: {

                        printk(KERN_INFO "SESSION SEMANTICS->Seeking session from first byte after end of buffer\n");

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

                        printk(KERN_INFO "SESSION SEMANTICS->Seeking session from current position in the buffer\n");

                        /*
                         * Return -EINVAL if the new requested position for the file pointer is beyond the actual
                         * limits of the opened file; also release mutex on session object
                         */

                        if ((file_pointer + offset > session->filesize) || (file_pointer + offset < 0)) {
                                printk(KERN_INFO "SESSION SEMANTICS->session_llseek returned an error: %d\n", -EINVAL);
                                mutex_unlock(&session->mutex);
                                return -EINVAL;
                        }

                        /*
                         * File pointer is moved by "offset" places
                         */

                        session->position += offset;
                        break;
                }
                case SEEK_SET: {

                        printk(KERN_INFO "SESSION SEMANTICS->Seeking session from first byte of buffer\n");

                        /*
                         * Return -EINVAL if the new requested position for the file pointer is beyond the actual
                         * limits of the opened file; also release mutex on session object
                         */

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
                 * Next page from the buffer to be flushed into the original file
                 */

                struct buffer_page* current_page;

                /*
                 * Index used to iterate through pages of the session buffer
                 */

                int i;

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

                printk(KERN_INFO "SESSION SEMANTICS->session_close will now truncate file %s\n",session->filename);
                ret = truncate_call(session->filename, 0);

                /*
                 * Return error code if truncate fails; before the
                 * session has to be removed, the module usage
                 * counter has to be decreased and the original
                 * memory segment has to be set
                 */

                if(ret) {
                        set_fs(segment);
                        session_remove(session);
                        module_put(THIS_MODULE);
                        printk(KERN_INFO "SESSION SEMANTICS->session_close could not truncate file and returned error: %d\n", ret);
                        return ret;
                }

                /*
                 * Initialize parameters for the write operation from the session
                 * object and the struct file of the opened file
                 */

                filesize = session->filesize;
                off = file->f_pos;

                /*
                 * Initialize "current_page" to the first element of the list of
                 * pages of the session
                 */

                current_page=list_entry(session->pages.next,struct buffer_page,buffer_pages_head);
                printk(KERN_INFO "SESSION SEMANTICS->session_close will now flush buffer page %d with base address %lu\nOffset:%lld",current_page->index,current_page->buffer_page_address,off);

                /*
                 * Set appropriate memory segment.
                 */

                set_fs(KERNEL_DS);

                /*
                 * Do differentiated work depending on the number of pages in the
                 * buffer
                 */

                printk(KERN_INFO "SESSION SEMANTICS->Number of pages:%d\n",session->nr_pages);
                if(session->nr_pages>1) {

                        /*
                         * Write first page of the buffer; note that file pointer of the original
                         * file gets updated so that next write will occur after last written byte
                         */

                        ret = session->f_ops_old->write(file, current_page->buffer_page_address, PAGE_SIZE,
                                                       &off);

                        /*
                         * If the number of bytes flushed to the original file is less
                         * than the size of the buffer, return -EIO (I/O error); first
                         * release the session, decrement the module usage counter and
                         * restore previous memory segment
                         */

                        if (ret < PAGE_SIZE) {
                                ret = -EIO;
                                set_fs(segment);
                                session_remove(session);
                                module_put(THIS_MODULE);
                                printk(KERN_INFO "SESSION SEMANTICS->session_close could not write all bytes because of error: %d\n", ret);
                                return ret;
                        }

                        /*
                         * Decrease filesize
                         */

                        filesize-=PAGE_SIZE;

                        /*
                         * Flush content of every remaining single page of the buffer into the original
                         * file
                         *
                         * In the following loop we write PAGE_SIZE bytes
                         */

                        for (i = 0; i < session->nr_pages-2; i++) {

                                /*
                                 * Get current page within the buffer
                                 */

                                current_page = list_entry(current_page->buffer_pages_head.next, struct buffer_page,
                                                          buffer_pages_head);
                                printk(KERN_INFO "SESSION SEMANTICS->session_close will now flush buffer page %d\nOffset:%lld\n",current_page->index,off);

                                /*
                                 * Write content of the page into the file
                                 */

                                ret = session->f_ops_old->write(file, current_page->buffer_page_address, PAGE_SIZE,
                                                                &off);

                                /*
                                 * If the number of bytes flushed to the original file is less
                                 * than the size of the buffer, return -EIO (I/O error)
                                 */

                                if (ret < PAGE_SIZE) {
                                        ret = -EIO;
                                        set_fs(segment);
                                        session_remove(session);
                                        module_put(THIS_MODULE);
                                        printk(KERN_INFO "SESSION SEMANTICS->session_close could not write all bytes because of error: %d\n", ret);
                                        return ret;
                                }

                                /*
                                 * Decrease filesize
                                 */

                                filesize-=PAGE_SIZE;

                        }

                        printk(KERN_INFO "SESSION SEMANTICS->session_close will now flush remaning bytes:%d",filesize);

                        /*
                         * Now we write last bytes (possibly less than PAGE_SIZE) into file
                         */

                        current_page = list_entry(current_page->buffer_pages_head.next, struct buffer_page,
                                                  buffer_pages_head);
                        printk(KERN_INFO "SESSION SEMANTICS->session_close will now flush buffer page %d\nBytes to copy:%d\nOffset:%lld",current_page->index,filesize%PAGE_SIZE,off);

                        /*
                         * Copy remaining bytes
                         */

                        if(filesize%PAGE_SIZE) {
                                ret = session->f_ops_old->write(file, current_page->buffer_page_address,
                                                                filesize % PAGE_SIZE, &off);

                                /*
                                 * Check that all bytes were written
                                 */

                                if (ret < filesize%PAGE_SIZE) {
                                        ret = -EIO;
                                        set_fs(segment);
                                        session_remove(session);
                                        module_put(THIS_MODULE);
                                        printk(KERN_INFO "SESSION SEMANTICS->session_close could not write all bytes because of error: %d\n", ret);
                                        return ret;
                                }
                        }
                        else {
                                ret = session->f_ops_old->write(file, current_page->buffer_page_address, PAGE_SIZE,
                                                                &off);

                                /*
                                 * Check that all bytes were written
                                 */

                                if (ret < PAGE_SIZE) {
                                        ret = -EIO;
                                        set_fs(segment);
                                        session_remove(session);
                                        module_put(THIS_MODULE);
                                        printk(KERN_INFO "SESSION SEMANTICS->session_close could not write all bytes because of error: %d\n", ret);
                                        return ret;
                                }
                        }
                }
                else {

                        /*
                         * Copy content of the only page
                         */

                        ret = session->f_ops_old->write(file, current_page->buffer_page_address, filesize % PAGE_SIZE,
                                                        &off);

                        /*
                         * Check that all bytes were written
                         */

                        if (ret < filesize%PAGE_SIZE) {
                                ret = -EIO;
                                set_fs(segment);
                                session_remove(session);
                                module_put(THIS_MODULE);
                                printk(KERN_INFO "SESSION SEMANTICS->session_close could not write all bytes because of error: %d\n", ret);
                                return ret;
                        }
                }

                /*
                 * Restore memory segment
                 */

                set_fs(segment);
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
         * Before returning decrement the module usage counter
         */

        printk(KERN_INFO "SESSION SEMANTICS->Decrementing module usage counter\n");
        module_put(THIS_MODULE);

        /*
         * Return outcome of the function
         */

        printk(KERN_INFO "SESSION SEMANTICS->session_close returned value: %d\n", 0);
        return 0;
}

/*
 * FILE OPERATIONS IN THE SESSION SEMANTICS - end
 */

/*
 * SESSION INIT - start
 *
 * Set the initial buffer used by the session to the pages filled with the content of the
 * file and initialize its mutex semaphore
 *
 * @session: session object to be initialized
 * @va: virtual address of the contiguous address space to be used as initial buffer of
 * the session
 * @filename: kernel-space filename of the opened file
 * @order: 2^order pages have been allocated to the inital buffer
 * @firstpage: pointer to the descriptor of the first among the frames allocated as initial
 * buffer
 * @filesize: number of bytes in the opened file
 *
 * Returns 0 in case of success, -ENOMEM if not enough memory is available for the
 * creation of the new objects and -EINVAL if any of the given pointer is NULL
 */

int session_init(struct session *session, void *va, const char *filename, int order,struct page* firstpage,loff_t filesize) {

        /*
         * Index value used in the pages loop (see below)
         */

        int i;

        printk(KERN_INFO "SESSION SEMANTICS->Initialising session\n");

        /*
         * Initialize the mutex
         */

        mutex_init(&session->mutex);

        /*
         * Set the file pointer to 0
         */

        session->position = 0;

        /*
         * Set the dirty flag to false
         */

        session->dirty = false;

        /*
         * Set the file length in the session object
         */

        session->filesize = filesize;

        /*
         * Store the filename into the session
         */

        session->filename=filename;

        /*
         * Store the number of pages in the initial buffer
         */

        session->nr_pages = (1 << order);

        /*
         * Initialize the link to the list of sessions
         */

        INIT_LIST_HEAD(&(session->link_to_list));

        /*
         * Initialize the the list of pages
         */

        INIT_LIST_HEAD(&(session->pages));

        /*
         * Store address of the descriptor and virtual address of the pages allocated
         * for the initial buffer into the session object
         */

        for(i=0;i<session->nr_pages;i++){

                /*
                 * Pointer to the new buffer_page object
                 */

                struct buffer_page* buffer_page;

                /*
                 * Create a new "buffer_page" object for each page of the buffer
                 */

                buffer_page=session_new_buffer_page(va+i*PAGE_SIZE,firstpage+i,i);

                /*
                 * Check if the creation of the new object was successful: if not,
                 * return the associated error code
                 */

                if(IS_ERR_VALUE(PTR_ERR(buffer_page)))
                        return PTR_ERR(buffer_page);
                /*
                 * Add the newly created object to the corresponding list in the session
                 * object
                 */

                printk(KERN_INFO "SESSION SEMANTICS->Adding buffer page %lu to session\n",buffer_page);
                list_add_tail(&buffer_page->buffer_pages_head,&(session->pages));
        }

        /*
         * Initialization of the session object was successful: return 0
         */

        printk(KERN_INFO "SESSION SEMANTICS->Session for file \"%s\" successfully initialised\n",session->filename);
        return 0;
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
 * SESSION OPEN - start
 *
 * Do all the work associated to the opening of a session.
 *
 * After the file has been opened using the original system call "open", the
 * content of the opened file is copied into some new pages dynamically allocated
 * bypassing the BUFFER CACHE.
 *
 * In this way the current process has its own copy of the file and can freely
 * modify it in such a way that modifications are not visible to other processes
 * that use the same file concurrently.
 *
 * @fd: file descriptor of the opened file
 * @filename: name of the file to be opened
 * @flags: flags to be used to determine the semantic of the file
 * @mode: permissions of the current process w.r.t. the opened file (mandatory if
 * O_CREAT flag is given)
 *
 * Returns 0 in case everything works, a negative error code otherwise
 */

int session_open(int fd,const char __user *filename, int flags, int mode){

        /*
         * Kernel representation of filename
         */

        const char* kernel_filename;

        /*
         * Return value
         */

        int ret;

        /*
         * Pointer to the descriptor of the first page in the buffer allocated to
         * read the file
         */

        struct page *first_page;

        /*
         * File object of the opened file
         */

        struct file *opened_file;

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
         * Size of the file to be opened
         */

        loff_t filesize;

        /*
         * Get the file object corresponding to the opened file
         */

        opened_file = get_file_from_descriptor(fd);

        /*
         * Get the size of the opened file
         */

        filesize = opened_file->f_dentry->d_inode->i_size;

        /*
         * ALLOCATE SESSION BUFFER
         *
         * Allocate the initial buffer associated to the current session
         *
         * The size is such that the whole file can be copied into it: 2^order
         * pages are allocated; if the file is empty, only one pages is allocated
         *
         * The pointer to the descriptor of the first page in the buffer is returned
         * and the order variable is set
         */

        first_page=session_create_buffer(filesize,filename,&order);

        /* Check that the pages have been successfully allocated:
         * return -ENOMEM in case not enough memory is available
         * for them
         */

        if (!first_page) {
                ret = -ENOMEM;
                printk(KERN_INFO "System call sys_session_open returned this error value:%d\n", ret);
                return ret;
        }

        /*
         * Map pages of the buffer into virtual address space
         */

        va = kmap(first_page);

        /*
         * Allocate a new session object
         */

        session = kmalloc(sizeof(struct session), GFP_KERNEL);

        /*
         * Check that the session object has  been successfully
         * allocated: return -ENOMEM in case not enough memory
         * is available and release allocated pages
         */

        if (!session) {
                ret = -ENOMEM;
                __free_pages(first_page, order);
                printk(KERN_INFO "System call sys_session_open returned this error value:%d\n", ret);
                return ret;
        }

        /*
         * INITIALIZE SESSION OBJECT - start
         *
         * First copy dynamically allocate a string to store the filename
         * into session; a string has a termination '\0' character which
         * is not counted by "strlen" function
         */

        kernel_filename=kmalloc(strlen(filename)+1,GFP_KERNEL);

        /*
         * Check enough memory is available: if not, release the session
         * object,release allocated pages and return -ENOMEM
         */

        if(!kernel_filename){
                ret = -ENOMEM;
                __free_pages(first_page, order);
                kfree(session);
                printk(KERN_INFO "System call sys_session_open returned this error value:%d\n", ret);
                return ret;
        }

        /*
         * Copy filename from user-space into kernel-space
         */

        copy_from_user(kernel_filename,filename,strlen(filename)+1);

        /*
         * Initialise the session object
         */

        ret=session_init(session, va, kernel_filename, order,first_page,filesize);

        /*
         * Check if the initialization of the session object: if not, free
         * allocated memory and return the error code
         */

        if(ret){
                struct buffer_page* buffer_page;
                struct buffer_page* temp;
                list_for_each_entry_safe(buffer_page,temp,&session->pages,buffer_pages_head){
                        kfree(buffer_page);
                }
                __free_pages(first_page, order);
                kfree(session);
                kfree(kernel_filename);
                printk(KERN_INFO "System call sys_session_open returned this error value:%d\n", ret);
                return ret;
        }

        /*
         * Save private data of the opened file (if any)
         */

        if(opened_file->private_data) {
                session->private=opened_file->private_data;
        }

        /*
         * INITIALIZE SESSION OBJECT - end
         */

        /*
         * If the file is not empty, copy its content into the allocated
         * session buffer
         */

        if(filesize) {

                /*
                 * COPY FILE INTO SESSION BUFFER - start
                 */

                /*
                 * Copy the content of the opened file into the session buffer, page by page,
                 * until a number of bytes equal to the filesize has been transferred
                 */

                ret=session_fill_buffer(first_page,order,opened_file);

                /*
                 * The function returns 0 when the request is successfully submitted:
                 * if this it not the case, we return the error code -EIO (I/O error)
                 * and release the allocated memory, although this should be unlikely
                 */

                if (ret) {
                        struct buffer_page* buffer_page;
                        struct buffer_page* temp;
                        list_for_each_entry_safe(buffer_page,temp,&session->pages,buffer_pages_head){
                                kfree(buffer_page);
                        }
                        __free_pages(first_page, order);
                        kfree(session);
                        kfree(kernel_filename);
                        ret = -EIO;
                        printk(KERN_INFO "System call sys_session_open returned this error value:%d\n", ret);
                        return ret;
                }

                /*
                 * COPY FILE INTO SESSION BUFFER - end
                 */
        }

        /*
         * Install the session in the opened file
         */

        ret=session_install(opened_file, session);

        /*
         * Check if the session has been properly installed:
         * if not, return corresponding error code and free
         * allocated memory
         */

        if(ret) {
                struct buffer_page *buffer_page;
                struct buffer_page *temp;
                list_for_each_entry_safe(buffer_page, temp, &session->pages, buffer_pages_head) {
                        kfree(buffer_page);
                }
                __free_pages(first_page, order);
                kfree(session);
                kfree(kernel_filename);
                ret = -EIO;
                printk(KERN_INFO "System call sys_session_open returned this value:%d\n", ret);
                return ret;
        }

        /*
         * Before returning, increment the usage counter of this module, so
         * it won't be possible to remove it while there's an ongoing I/O
         * session. The counter will be decremented when the session is
         * explicitly closed
         */

        printk(KERN_INFO "SESSION SEMANTICS->Incrementing module usage counter\n");
        try_module_get(THIS_MODULE);

        /*
         * The session has been successfully opened: print file descriptor
         * of the file that is now opened adopting the session semantics
         * and return 0
         */

        printk(KERN_INFO "System call sys_session_open returned this value:%d\n", fd);
        return 0;
}

/*
 * SESSION OPEN - end
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
         * Open file using the original sys_open system call ignoring the flag
         * used to request the session semantics (for the time being)
         */

        if (flags & SESSION_OPEN) {
                fd = previous_open(filename, flags & ~SESSION_OPEN, mode);
                printk(KERN_INFO "SESSION SEMANTICS->Flags for filename \"%s\": %d; file descriptor:%d\n", filename,
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
                 * Return value
                 */

                int ret;

                /*
                 * Open session
                 */

                ret=session_open(fd,filename,flags,mode);

                /*
                 * Return code in case of error
                 */

                if(ret)
                        return ret;
        }

        /*
         * Return the descriptor of the opened file or an error code in case opening failed
         */

        return fd;
}
