#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char* argv[]) {
    
    int pid, i;
    pid = fork();

    if(pid == 0) {
        for(i=0; i<50; i++) {
            sleep(10);
            printf(1, "child\n");
            yield();
        }
    } else if(pid > 0) {
        for(i=0; i<50; i++) {
            sleep(10);
            printf(1, "parent\n");
            yield();
        }
    } else {
        printf(1, "error occur\n");
        exit();
    }
   
    wait();
    exit();
    return 0;
}
