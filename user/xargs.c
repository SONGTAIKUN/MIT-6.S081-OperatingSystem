#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define TOKSZ 128  // The maximum length of a single token

/*
By default, xargs on UNIX performs an optimization: it tries to pass multiple arguments read 
from standard input to the command all at once, minimizing fork/exec cycles.

This experiment doesn't require you to implement this optimization: our goal is to pass only 
one argument to the command at a time, executing the command individually for each argument.
If you're using the native xargs for comparison testing, please specify -n 1 to ensure it 
behaves as intended.
*/

static void
run_with_token(char **fixed, int fcnt, char *word) {
  char *args[MAXARG];
  int j, idx = 0;

  for (j = 0; j < fcnt; j++) args[idx++] = fixed[j];
  args[idx++] = word;
  args[idx] = 0;

  int pid = fork();
  if (pid == 0) {
    exec(args[0], args);
    fprintf(2, "xargs: exec %s failed\n", args[0]);
    exit(1);
  } else if (pid > 0) {
    wait(0);
  } else {
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  }
}

static void
run_with_tokens(char **fixed, int fcnt, char **words, int wcnt) {
  char *args[MAXARG];
  int j, idx = 0;

  for (j = 0; j < fcnt; j++) args[idx++] = fixed[j];
  for (j = 0; j < wcnt; j++)  args[idx++] = words[j];
  args[idx] = 0;

  int pid = fork();
  if (pid == 0) {
    exec(args[0], args);
    fprintf(2, "xargs: exec %s failed\n", args[0]);
    exit(1);
  } else if (pid > 0) {
    wait(0);
  } else {
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  }

  // This batch of parameters has been used up and released one by one
  for (int j = 0; j < wcnt; j++) {
    free(words[j]);
    words[j] = 0;    
    /*
    * After calling free , words[j] still holds the original address, but that memory block no 
    * longer belongs to you—it's a "dangling pointer." Setting it to NULL explicitly marks it 
    * as invalid, preventing subsequent misuse.
    */     
  }
}


/*
This is the latest version of xargs.c:
Without the -n flag, all tokens read from stdin are accumulated and executed at once (achieving the 
goal of minimizing fork/exec cycles. This is, of course, a simplified version of "merged execution," 
not the full optimization of UNIX xargs, which dynamically batches execution based on the system's ARG limit).

With -n 1, per-token mode is switched: a separate fork/exec is performed for each token.
In other words, -n 1 disables "merged execution," forcing execution to run one line/one argument at a time.
*/
int
main(int argc, char *argv[]) {
  int i = 1;
  int per_token = 0; 

  if (argc >= 3 && strcmp(argv[1], "-n") == 0) {    
    //First check whether the number of parameters is at least 3 (to prevent out-of-bounds access), and then 
    //confirm that the first parameter is indeed -n
    if (strcmp(argv[2], "1") != 0) {
      /*
      * Forces the command to accept only the -n 1 option (the experimental requirement is "only one argument at a time").
      * If it is not "1" (for example, -n 2), exit with an error.
      */
      fprintf(2, "xargs: only -n 1 supported\n");
      exit(1);
    }
    per_token = 1;
    i = 3;  // Subcommands start from argv[3]
  }

  /*
  * Back-end verification: After parsing -n 1, confirm that there are subcommands to be executed; otherwise, report 
  * the usage and exit to avoid crash due to lack of subsequent commands to execute.
  */
  if (i >= argc) {
    fprintf(2, "usage: xargs [-n 1] <cmd> [args...]\n");
    exit(1);
  }


  /*
  * This code extracts the subcommand to be executed and its fixed parameters from argv, stores them in a separate 
  * array fixed[], and records the number in fcnt, so that it can be easily appended with the token read from the 
  * standard input to exec.
  */
  char *fixed[MAXARG];
  int fcnt = 0;
  for (int k = i; k < argc; k++) {
    fixed[fcnt++] = argv[k];
  }

  char tok[TOKSZ];
  int  tlen = 0;
  char ch;
  int  r;

  // When -n is not specified, all tokens are accumulated.
  char *words[MAXARG];
  int   wcnt = 0;

  /*
  *  The 0 in read(0, &ch, 1) is the file descriptor for standard input (stdin).
  *  In Unix/xv6, a process starts with three open file descriptors by default:
  *  0 → standard input (stdin)
  *  1 → standard output (stdout)
  *  2 → standard error (stderr)
  */
  while ((r = read(0, &ch, 1)) == 1) {
    // ignore '' and \'
    if (ch == '"' || ch == '\'') {
      continue;
    }

    // Handle backslash escapes: treat "\n" as newline separator
    if (ch == '\\') {
      /*
      * The backslash \ in C source code is the "escape sequence character".
      * To represent a backslash character in source code, you must write it as an escaped literal: '\\'
      * The two characters \ \ here are just source code notation. After compilation, they represent one character (ASCII 92).
      * Analogy: '\n' is written as two characters \n in the source code, but it represents a "line feed" character (ASCII 10).
      */

      char ch2;

      //After entering if (ch == '\\'), we read the next character ch2.
      int r2 = read(0, &ch2, 1);

      /*
      * This section terminates the current token when a delimiter is encountered. If -n 1 is specified, the program is 
      * executed immediately. Otherwise, the program is saved and protected against MAXARG overflow and persisted in memory.
      */
      if (r2 == 1 && ch2 == 'n') {
        if (tlen > 0) {   //Only when there is a token accumulated in hand will it be closed
          tok[tlen] = 0;   //Add '\0' to tok to turn the temporary buffer into a valid C string
          if (per_token) {
            run_with_token(fixed, fcnt, tok);   //Triggered by command line -n ​​1, each parameter is forked/exec'd separately (executed one by one).
          } else {
            if (fcnt + wcnt + 1 >= MAXARG) {   // +1 is reserved for "the token that is about to be added"

              // If adding this token will exceed the exec parameter limit (MAXARG),
              // Then execute the collected tokens once (flush), then clear the accumulation
              run_with_tokens(fixed, fcnt, words, wcnt);
              wcnt = 0;
            }

            // Allocate a separate memory block for the current token and copy the contents of tok into it.
            // (Because tok is a reusable temporary buffer and will be overwritten in the next round, a persistent copy must be made)
            char *s = malloc(tlen + 1);
            if (s == 0) { fprintf(2, "xargs: malloc failed\n"); exit(1); }
            memmove(s, tok, tlen + 1);  // Copy the complete string including '\0' (so tlen+1)
          }
          tlen = 0;
        }
        continue;
      } else {
        // Not \n: treat the backslash and the next character as normal characters
        if (tlen < TOKSZ - 1) tok[tlen++] = '\\';
        /*
        * Meaning: Puts a backslash character (\) into the token.
        * Condition: tlen < TOKSZ - 1: Leaves the last byte for the future string terminator ('\0') to prevent overflow.
        */

        if(r2 == 1 && tlen < TOKSZ - 1) tok[tlen++] = ch2;
        // If you did read the "character after the backslash" just now (r2 == 1), add this character to the token as is.
        continue;
      }
    }

    // Whitespace separation: space/Tab/line break
    if (ch == ' ' || ch == '\t' || ch == '\n') {
      if (tlen > 0) {
        tok[tlen] = 0;
        if (per_token) {
          run_with_token(fixed, fcnt, tok);
        } else {
          if (fcnt + wcnt + 1 >= MAXARG) {   
            run_with_tokens(fixed, fcnt, words, wcnt);
            wcnt = 0;
          }
          char *s = malloc(tlen + 1);
          if (s == 0) { fprintf(2, "xargs: malloc failed\n"); exit(1); }
          memmove(s, tok, tlen + 1);  
          words[wcnt++] = s;
        }
        tlen = 0;
      }
      continue;
    }

    // Adding common characters to tokens
    if (tlen < TOKSZ - 1) tok[tlen++] = ch;
  }

  // EOF: Process remaining tokens
  /*
  * Your loop will only close and process the current token when it encounters a delimiter (space/Tab/\n, or a \n escape 
  * sequence that you've specifically identified as a delimiter). If there's no delimiter after the last input argument 
  * (commonly seen in: printf "a b", echo -n a, or when the last line of a file doesn't have a newline), the loop will exit 
  * when read() returns 0 (EOF), but tokens with tlen > 0 have not yet been committed.
  */
  if (tlen > 0) {
    tok[tlen] = 0;
    if (per_token) {
      run_with_token(fixed, fcnt, tok);
    } else {
      if (fcnt + wcnt + 1 >= MAXARG) {
        run_with_tokens(fixed, fcnt, words, wcnt);
        wcnt = 0;
      }
      char *s = malloc(tlen + 1);
      if (s == 0) { fprintf(2, "xargs: malloc failed\n"); exit(1); }
      memmove(s, tok, tlen + 1);
      words[wcnt++] = s;
    }
  }

  // No -n: one-time execution
  if (!per_token && wcnt > 0) {
    run_with_tokens(fixed, fcnt, words, wcnt);
    wcnt = 0;
  }

  exit(0);
}
