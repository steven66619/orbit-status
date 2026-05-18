#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

struct ConfigEntry {
    char key[64];
    char value[256];
};

struct Config {
    int n_entries = 0;
    ConfigEntry entries[128];
};

static inline char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = 0;
    return s;
}

static inline Config *config_load(const char *path) {
    Config *cfg = (Config *)calloc(1, sizeof(Config));
    if (!cfg) return nullptr;

    FILE *fp = fopen(path, "r");
    if (!fp) return cfg;

    char line[512];
    while (fgets(line, sizeof(line), fp) && cfg->n_entries < 128) {
        char *p = trim(line);
        if (*p == 0 || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(p);
        char *val = trim(eq + 1);

        ConfigEntry *e = &cfg->entries[cfg->n_entries++];
        snprintf(e->key, sizeof(e->key), "%s", key);
        snprintf(e->value, sizeof(e->value), "%s", val);
    }

    fclose(fp);
    return cfg;
}

static inline const char *config_get(Config *cfg, const char *key, const char *def) {
    if (!cfg) return def;
    for (int i = 0; i < cfg->n_entries; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return cfg->entries[i].value;
    }
    return def;
}

static inline int config_get_int(Config *cfg, const char *key, int def) {
    const char *val = config_get(cfg, key, nullptr);
    if (!val) return def;
    char *end = nullptr;
    long n = strtol(val, &end, 10);
    if (end == val) return def;
    return (int)n;
}

static inline int config_get_color(Config *cfg, const char *key, float out[4], const float def[4]) {
    const char *val = config_get(cfg, key, nullptr);
    if (!val) {
        if (def) { out[0]=def[0]; out[1]=def[1]; out[2]=def[2]; out[3]=def[3]; }
        return 0;
    }
    float r, g, b, a = 1.0f;
    int n = sscanf(val, "%f %f %f %f", &r, &g, &b, &a);
    if (n >= 3) { out[0]=r; out[1]=g; out[2]=b; out[3]=a; return 1; }
    if (def) { out[0]=def[0]; out[1]=def[1]; out[2]=def[2]; out[3]=def[3]; }
    return 0;
}

static inline void config_destroy(Config *cfg) {
    free(cfg);
}

#endif
