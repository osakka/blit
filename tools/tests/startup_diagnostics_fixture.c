#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/blit_trial_protocol.h"
#include "../../src/blit_startup_diagnostics.h"
int main(void)
{
    blit_startup_stage("fixture",57);
    blit_startup_modules();
    return 0;
}
