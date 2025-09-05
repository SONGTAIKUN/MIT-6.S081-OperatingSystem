#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


// Why the if/else structure is correct?
// fork() returns twice: in the child it returns 0, in the parent it returns the child’s PID (>0).
// In the same source code, we use `if (pid == 0)` to choose the child’s execution path,
// and `else` to choose the parent’s execution path.
// These two branches execute in their own processes. Which one runs first is decided
// by the OS scheduler, not by the if/else order in the code.

// How is the communication order guaranteed? (parent write → child read → child write → parent read)
// It relies on the blocking semantics of pipes:
//   - read(fd, …): blocks if the pipe is empty, until the other side writes or all write ends are closed (then returns 0 = EOF).
//   - write(fd, …): blocks only if the pipe is full (not the case here since we write only 1 byte).
// Therefore, since the child first calls read(p2c[0]), it blocks until the parent writes to p2c[1].
// After the child writes back to c2p[1], the parent’s read(c2p[0]) can succeed.
// This “natural synchronization” ensures the correct ping-pong order without extra locks.


int 
main(int argc, char *argv[]){

    int p2c[2];
    int c2p[2];

    if (pipe(p2c) < 0 || pipe(c2p) < 0) {
        fprintf(2, "pipe() failed\n");
        exit(1);
    }

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork() failed\n");
        exit(1);
    }

    if (pid == 0) {
        // ===== Child Process =====
        // Turn off unused terminals first
        //The read end of the pipe: 0, The write end of the pipe: 1
        close(p2c[1]);
        close(c2p[0]);

        // 1. Meaning of read(p2c[0], &byte, 1)
        // Function prototype: ssize_t read(int fd, void *buf, size_t count);
        // fd: file descriptor, tells the kernel where to read from.
        // buf: buffer address, the kernel writes the read data here.
        // count: the number of bytes expected to read.
        // In this case:
        //   p2c[0] is the read end of the pipe (the parent writes to p2c[1]).
        //   &byte is a pointer to 1 byte of memory to store the data.
        //   1 means we want to read exactly 1 byte.
        // So this call reads 1 byte from pipe p2c[0] into variable 'byte'.

        // 2. Return value of read()
        // On success: returns the number of bytes actually read (expected 1 here).
        // On EOF (end of file/pipe closed): returns 0.
        // On error: returns -1.
        char byte;
        if (read(p2c[0], &byte, 1) != 1) {
            fprintf(2, "child: read error\n");
            exit(1);
        }

        //int getpid(): Return the current process’s PID.
        printf("%d: received ping\n", getpid());

        if (write(c2p[1], &byte, 1) != 1) {
            fprintf(2, "child: write error\n");
            exit(1);
        }

        close(p2c[0]);
        close(c2p[1]);
        exit(0);
    } else {
        close(p2c[0]);
        close(c2p[1]);

        char byte = 'x';
        if (write(p2c[1], &byte, 1) != 1) {
            fprintf(2, "parents: write error\n");
            exit(1);
        }

        if (read(c2p[0], &byte, 1) != 1) {
            fprintf(2, "parents: read error\n");
            exit(1);
        }

        printf("%d: received pong\n", getpid());

        close(p2c[1]);
        close(c2p[0]);

        // wait(0) (in Linux usually written as wait(NULL)) blocks the parent
        // until a child process terminates and then "reaps" it. This is important:
        //
        // 1) Prevent zombie processes:
        //    After a child calls exit(), the kernel does not fully remove it.
        //    Instead it marks the child as ZOMBIE, keeping a small process table entry
        //    (including its exit status) until the parent reads it.
        //    The parent must call wait()/waitpid() to release these resources.
        //    If the parent never waits while still running, zombies accumulate
        //    and leak process table slots.
        //
        // 2) Clear termination order / synchronization point:
        //    The ping-pong logic is already synchronized via pipe blocking semantics
        //    (parent write → child read → child write → parent read).
        //    But for the program as a whole, it is better if the parent confirms
        //    the child has fully terminated before exiting.
        //    Otherwise, a race can occur: the parent exits first, the shell prints
        //    its prompt, while the child is still printing output — leading to
        //    messy interleaved output. wait(0) ensures the parent exits last.
        wait(0);
        exit(0);
    }

    exit(0);

}