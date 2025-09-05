#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"


//int fstat(int fd, struct stat *st) Place info about an open file into *st.
//strcpy(buf, path) It copies the string path ending with '\0' to buf byte by byte, and also copies 
//the '\0' at the end.

/*
* memmove(p, de.name, DIRSIZ) copies DIRSIZ bytes of the name field de.name in the directory entry 
* to the location pointed to by p . Here, p is the pointer to the path buffer, appended with a '/'. 
* This is the starting position of the subfile name in buf .
*/

static char*
basename(char *path) {
  /*
  * Does path have to end with '\0'?
  * This is a basic convention for C language strings, not a "Unix/xv6 special requirement". As long 
  * as the parameter type is a "string" of char *, it ends with '\0' by default.
  * xv6's system calls/library functions (e.g., open(path), stat(path)) all expect you to pass in C-style 
  * strings, so they must have a terminating '\0'.
  */
  char *p = path + strlen(path);    // ① p points to the end of the string '\0'
  while (p >= path && *p != '/')    // ② Find the first '/' from the end to the left
    p--;
  return p + 1; 
  /*
  * What is returned is a pointer to the starting position of the file name. No matter how many characters 
  * the file name has, for example, aabbcc, it will point to the first character a; the subsequent abbcc will 
  * still be in the same continuous block of memory, and C string functions (printf/strcmp, etc.) will read 
  * until the trailing '\0'.
  */
  
}

static void
find(char *path, char *target) {
  int fd;
  struct stat st;

  // 1) Open path
  if ((fd = open(path, 0)) < 0) {              // 0 = O_RDONLY
    fprintf(2, "find: cannot open %s\n", path);
    return;                                     
  }

  // 2) Get metadata such as type/size
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  // 3) Determine whether it is a file or directory based on st.type (xv6: T_FILE/T_DIR/T_DEVICE)
  switch (st.type) {
  case T_FILE: {
    char *bn = basename(path);
    if (strcmp(bn, target) == 0) //strcmp compares basename and targetname for exact equality (byte by byte until '\0').
      printf("%s\n", path);
    close(fd);
    return;
  }

  case T_DIR:{
    char buf[512], *p;
    struct dirent de;

    // Reserved: path + '/' + subname (DIRSIZ) + '\0'
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        fprintf(2, "find: path too long\n");
        close(fd);
        return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    // Read the directory item by item
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue; //This is skipping "empty slots" in the directory. 
        /*
        * In xv6, a directory is composed of many fixed-size directory entries (struct dirent).
        * When a directory entry is valid (a file or subdirectory actually occupies it), inum is 
        * set to the inode number of that file or directory (non-zero).
        * When a directory entry is unused or deleted, the kernel sets its inum to 0, indicating 
        * that the slot is empty.
        */

        // de.name is fixed DIRSIZ bytes, may not have '\0', first convert it to a C string
        char name[DIRSIZ + 1];
        memmove(name, de.name, DIRSIZ);  //Copy the directory entry name to name[]
        name[DIRSIZ] = 0; //add '\0'
        //name[DIRSIZ] = 0; is equivalent to name[DIRSIZ] = '\0';. Writing '\0' is more intuitive to express "string terminator".
        //Be careful not to write '0' (ASCII character '0', value 48), which is not a terminator.

        // Must skip . and ..
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        continue;

        // Spell the full path to buf = "path/name"
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0; //add '\0'

        // Recursively enter subpaths
        find(buf, target);
    }
    close(fd);
    break;
  }

  default:
    // Other types (devices, etc.) are ignored.
    close(fd);
    break;
  }
}

int
main(int argc, char *argv[]){

  if (argc != 3) {
    fprintf(2, "usage: find <start-path> <target-name>\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);

}