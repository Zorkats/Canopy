/* A smoke check of libcanopy_core.so the way a host loads it: dlopen, then
 * three exports looked up by name and called with a game folder. It prints
 * the ABI, ok, the build stamp, supported and the file counts, never game
 * text. Built off Windows only.
 *
 *   canopy_core_smoke [library] [game folder]
 *
 * The library defaults to ./libcanopy_core.so, the game folder to
 * CANOPY_GAME_DIR, then the usual Steam install. A missing game prints a
 * note and passes. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*AbiFunction)(void);
typedef char* (*FolderFunction)(const char*);
typedef void (*FreeFunction)(char*);

/* dlsym gives an object pointer; POSIX makes it safe to read one as a
 * function pointer, and copying the bytes keeps ISO C quiet about it. */
static void Symbol(void* handle, const char* name, void* function, size_t size) {
    void* found = dlsym(handle, name);
    memset(function, 0, size);
    if (found && size == sizeof(found)) {
        memcpy(function, &found, size);
    }
}

/* The text after "key": in json, up to the next ',' or '}', into value. */
static int Field(const char* json, const char* key, char* value, size_t size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* at = strstr(json, pattern);
    if (!at || size == 0) {
        return 0;
    }
    at += strlen(pattern);
    size_t length = strcspn(at, ",}");
    if (length >= size) {
        length = size - 1;
    }
    memcpy(value, at, length);
    value[length] = '\0';
    return 1;
}

static size_t Count(const char* text, const char* part) {
    size_t count = 0;
    const size_t length = strlen(part);
    for (const char* at = strstr(text, part); at; at = strstr(at + length, part)) {
        ++count;
    }
    return count;
}

static int Exists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fclose(file);
    return 1;
}

int main(int argc, char** argv) {
    const char* library = argc > 1 ? argv[1] : "./libcanopy_core.so";
    const char* game = argc > 2 ? argv[2] : getenv("CANOPY_GAME_DIR");
    char fallback[1024];
    if (!game || !*game) {
        const char* home = getenv("HOME");
        snprintf(fallback, sizeof(fallback), "%s/.local/share/Steam/steamapps/common/Village in the Shade",
                 home ? home : "");
        game = fallback;
    }

    void* handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        printf("FAILED: dlopen: %s\n", dlerror());
        return 1;
    }
    AbiFunction abi;
    FolderFunction gameJson;
    FolderFunction filesJson;
    FreeFunction release;
    Symbol(handle, "canopy_core_abi", &abi, sizeof(abi));
    Symbol(handle, "canopy_core_game", &gameJson, sizeof(gameJson));
    Symbol(handle, "canopy_core_files", &filesJson, sizeof(filesJson));
    Symbol(handle, "canopy_core_free", &release, sizeof(release));
    if (!abi || !gameJson || !filesJson || !release) {
        printf("FAILED: an export is missing\n");
        dlclose(handle);
        return 1;
    }
    printf("abi %d\n", abi());

    char data[1100];
    snprintf(data, sizeof(data), "%s/data.dat", game);
    if (!Exists(data)) {
        printf("note: no data.dat in the game folder, game calls skipped\n");
        dlclose(handle);
        return 0;
    }

    int failed = 0;
    char* text = gameJson(game);
    char ok[16] = "";
    char build[32] = "";
    char supported[16] = "";
    char files[32] = "";
    if (!text || !Field(text, "ok", ok, sizeof(ok)) || !Field(text, "build", build, sizeof(build)) ||
        !Field(text, "supported", supported, sizeof(supported)) || !Field(text, "files", files, sizeof(files))) {
        printf("FAILED: canopy_core_game gave no ok, build, supported or files\n");
        failed = 1;
    } else {
        printf("game: ok %s, build %s, supported %s, files %s\n", ok, build, supported, files);
        failed = strcmp(ok, "true") != 0;
    }
    release(text);

    text = filesJson(game);
    if (!text || !Field(text, "ok", ok, sizeof(ok))) {
        printf("FAILED: canopy_core_files gave no ok\n");
        failed = 1;
    } else {
        const size_t count = Count(text, "{\"path\":");
        printf("files: ok %s, %zu files, %zu bytes of JSON\n", ok, count, strlen(text));
        failed = failed || strcmp(ok, "true") != 0 || count == 0;
    }
    release(text);
    dlclose(handle);
    printf("%s\n", failed ? "FAILED" : "smoke ok");
    return failed;
}
