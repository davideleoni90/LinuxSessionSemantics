//
// Created by leo on 27/09/16.
//

#ifndef SESSIONFILE_SESSION_H
#define SESSIONFILE_SESSION_H

/*
 * When a process wants to start an I/O session relative to a certain file,
 * the following flag has to be bitwise-ORed with the other flags given in
 * the open system call
 */

#define SESSION_OPEN 00000004


/*
 * Structure to handle an I/O session on a file
 *
 * buffer: pointer to the content of the file during a session
 * lock: spinlock to be used to synchronize read and write operations on the
 * file during a session
 * position: offset with respect to the beginning of the file from which
 * the next I/O operation will take place during a session
 * filesize: number of bytes in the file
 * read: pointer to the former read operations for the file opened adopting
 * a session semantics
 * write: pointer to the former write operations for the file opened adopting
 * a session semantics
 * llseek: pointer to the former llseek operations for the file opened adopting
 * a session semantics
 * dirty: indicates that the session buffer has been modified, so as the session
 * gets closed the modifications have to be propagated to the original file
 * filename: string representing the filename in the user-space
 */

struct session{
        void* buffer;
        spinlock_t lock;
        loff_t position;
        loff_t filesize;
        ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
        ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
        loff_t (*llseek) (struct file *, loff_t, int);
        bool dirty;
        const char __user* filename;
};

/*
 * FUNCTION PROTOTYPES - start
 */

extern asmlinkage long (*previous_open)(const char __user* filename,int flags,int mode);
extern asmlinkage long sys_session_open(const char __user* filename,int flags,int mode);
void cleanup_sessions(void);

/*
 * FUNCTION PROTOTYPES - end
 */

#endif //SESSIONFILE_SESSION_H
