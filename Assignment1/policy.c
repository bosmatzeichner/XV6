#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{    
    if(argc != 2){
        printf(2, "Usage: bad request\n");
        exit(0);
    }    
    policy(atoi(argv[1]));
    exit(0);
}