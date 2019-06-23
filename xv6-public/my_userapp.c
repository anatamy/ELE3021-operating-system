#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
    int ret_val;
    ret_val = getpid();
    printf(1, "My pid is: %d\n", ret_val);
    ret_val = getppid();
    printf(1, "My ppid is: %d\n", ret_val);
    
    exit();
}
