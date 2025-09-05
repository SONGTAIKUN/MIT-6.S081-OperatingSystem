#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int 
main(int argc, char *argv[]){

    //If the user forgets to pass an argument, sleep should print an error message.
    if (argc < 2) {                    
        fprintf(2, "usage: sleep ticks\n");
        exit(1);
    }

    int i = atoi(argv[1]);   //Convert a string to an integer

    sleep(i);      

    exit(0);
}