#include "systemcalls.h"
#include <stdio.h>

int main() {
    // Test the do_system function
    if (do_system("ls")) {
        printf(" do_system: system('ls') ...OK\n");
    } else {
        printf("do_system: system('ls') ...FAIL\n");
    }

    // Test the do_exec function
    if (do_exec(2, "/bin/ls", "-l")) {
        printf("do_exec: execv('/bin/ls -l') ...OK\n");
    } else {
        printf("do_exec: execv('/bin/ls -l') ...FAIL\n");
    }

    // Test the do_exec_redirect function
    if (do_exec_redirect("/tmp/output.txt", 2, "/bin/ls", "-l")) {
        printf("do_exec_redirect: execv('/bin/ls -l' > /tmp/output.txt) ...OK\n");
    } else {
        printf("do_exec_redirect: execv('/bin/ls -l' > /tmp/output.txt) ...FAIL\n");
    }

    return 0;
}

