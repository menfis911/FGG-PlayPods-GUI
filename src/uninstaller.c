#include "log.h"
#include "tile.h"

#include <stdio.h>

#define STATE_DIR  "/data/fgg-playpods-gui"
#define UNINSTALL_LOG STATE_DIR "/audiobridge-uninstall.log"

int main(void)
{
    int result;
    if (!log_open(STATE_DIR, UNINSTALL_LOG)) return 1;
    log_line("AudioBridge tile uninstaller");
    result = tile_uninstall();
    log_close();
    return result == 0 ? 0 : 1;
}
