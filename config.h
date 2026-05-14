#ifndef CONFIG_H
#define CONFIG_H

struct config_entry {
    char key[64];
    char value[256];
};

struct config {
    int n_entries;
    struct config_entry entries[128];
};

struct config *config_load(const char *path);
const char *config_get(struct config *cfg, const char *key, const char *def);
int config_get_int(struct config *cfg, const char *key, int def);
int config_get_color(struct config *cfg, const char *key, float out[4], const float def[4]);
void config_destroy(struct config *cfg);

#endif
