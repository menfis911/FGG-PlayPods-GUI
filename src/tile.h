#ifndef AUDIOBRIDGE_TILE_H
#define AUDIOBRIDGE_TILE_H

#define AUDIOBRIDGE_TITLE_ID "ABRG18195"

/* Installs the dashboard shortcut when absent and refreshes its owned files
 * when the payload version changes. A title-id collision is never overwritten. */
int tile_install_or_update(const char *version);

/* Removes only the registered shortcut whose ownership marker matches
 * AudioBridge. The stable pairing/state directory is intentionally retained. */
int tile_uninstall(void);

#endif
