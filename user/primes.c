#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void feed_numbers(int wfd) {
  for (int i = 2; i <= 35; i++) {
    if (write(wfd, &i, sizeof(i)) != sizeof(i)) {
      fprintf(2, "writer: write error\n");
      exit(1);
    }
  }
}

/*
* leftfd is a file descriptor number that points to the read end of the pipe from which the current sieve layer 
* will read data.
* Because it occupies kernel and process resources, it must be released after use using close(leftfd). Furthermore, 
* when there are multiple layers of processes/pipes, not closing unused ends may affect the transmission of EOF or 
* cause resource leaks.
*/
static void sieve(int leftfd) {
  int p;
  //Read the first number in this layer; if there is no data, end
  if (read(leftfd, &p, sizeof(p)) != sizeof(p)) {
    close(leftfd);
    exit(0);
  }

  //In the "sieve" structure, the first number p read by each process must be a prime number.
  printf("prime %d\n", p);

  //Pre-read whether there are still numbers that need to be screened (the first number that is not a multiple of p) (And x is the next number of p)
  int x, first_nonmul = 0, have_next = 0;
  while (read(leftfd, &x, sizeof(x)) == sizeof(x)) {
    if (x % p != 0) {
      first_nonmul = x;
      have_next = 1;
      break;
    }
  }

  if (!have_next) {
    //There are no more numbers to be sieved, and this layer ends.
    close(leftfd);
    exit(0);
  }

  //There are still numbers to be screened: create the right pipe and fork the next layer
  int next[2];
  if (pipe(next) < 0) {
    fprintf(2, "pipe failed\n");
    close(leftfd);
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork failed\n");
    close(leftfd);
    close(next[0]);
    close(next[1]);
    exit(1);
  }

  if (pid == 0) {
    // Next layer: read only next[0], recursive processing
    close(next[1]);
    close(leftfd);
    sieve(next[0]);  //Just create a read buffer, but don't actually write data to next[0]
    // Will not return
    exit(0);
  }

  // Current level parent: forward non-p multiples to next[1]
  close(next[0]);
  /*
  * The parent closes its own copy of next[0].
  * After fork(), the parent and child each have a file descriptor table entry. The parent 
  * process closes its read end only; the child process's next[0] remains open and can read() 
  * as usual.
  */

  // First write the first non-multiple that has been pre-read
  if (write(next[1], &first_nonmul, sizeof(first_nonmul)) != sizeof(first_nonmul)) {
    fprintf(2, "write error\n");
  }

  /*
  * Read all the numbers in leftfd after first_nonmul, but if they are not multiples of p (that is, they may 
  * be prime numbers), write them back to next for the next child sieve to re-screen
  */
  while (read(leftfd, &x, sizeof(x)) == sizeof(x)) {
    if (x % p != 0) {
      if (write(next[1], &x, sizeof(x)) != sizeof(x)) {
        fprintf(2, "write error\n");
        break;
      }
    }
  }

  // Closing and recycling
  close(leftfd);
  close(next[1]);   // Trigger right EOF
  wait(0);          // Recycle the next layer
  exit(0);
}


int 
main(int argc, char *argv[]){

    /*
    * In Unix/xv6, a file descriptor (fd) is a small per-process integer index.
    * By convention:
    *    0 -> standard input  (stdin)
    *    1 -> standard output (stdout)
    *    2 -> standard error  (stderr)
    *
    * When you call pipe(p), the kernel allocates the two lowest available fds
    * for the pipe’s read end and write end. Because 0/1/2 are already taken,
    * the next free slots are typically 3 and 4, so you often see:
    *    p[0] == 3  // read end
    *    p[1] == 4  // write end
    *
    * Note: the exact numbers aren’t guaranteed—if other fds are open, the
    * lowest available integers at that moment will be used.
    */
    int p[2];
    if (pipe(p) < 0) {
        fprintf(2, "pipe() failed\n");
        exit(1);
    }

    int pid = fork();
    if (pid < 0) {
        /*
        * 0: Standard input (stdin)
        * 1: Standard output (stdout)
        * 2: Standard error (stderr)
        */
        fprintf(2, "fork() failed\n");
        exit(1);
    }

    //The parent and child processes read and write at the same time, and the buffer determines who may need to be suspended (blocked)
    if (pid == 0) {
        close(p[1]);  
        sieve(p[0]);
        exit(0);
    } else {
        close(p[0]);        
        feed_numbers(p[1]); 
        close(p[1]);        
        wait(0);            
        exit(0);
    }
    exit(0);
}