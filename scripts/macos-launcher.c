/* Native .app launcher: keep the shared Windows product executable unchanged. */
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char executable[PATH_MAX], resolved[PATH_MAX];
    uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) || !realpath(executable, resolved)) {
        perror("Cannot locate Retrocycles RCL launcher"); return 1;
    }
    char *slash = strrchr(resolved, '/');
    if (!slash) return 1;
    *slash = '\0'; /* Contents/MacOS */
    char binary[PATH_MAX], data[PATH_MAX], config[PATH_MAX], profile[PATH_MAX];
#define PATH(dst, fmt, arg) if (snprintf(dst, sizeof(dst), fmt, arg) >= (int)sizeof(dst)) return 1
    PATH(binary, "%s/armagetronad", resolved);
    PATH(data, "%s/../Resources", resolved);
    PATH(config, "%s/config", data);
    const char *home = getenv("HOME");
    if (!home) { fputs("HOME is unavailable\n", stderr); return 1; }
    PATH(profile, "%s/Library/Application Support/Retrocycles RCL Client", home);
    /* SDL12-compat finds bundled SDL2 alongside its own library. */
    char **args = calloc((size_t)argc + 7, sizeof(*args));
    if (!args) return 1;
    args[0] = binary;
    args[1] = "--datadir"; args[2] = data;
    args[3] = "--configdir"; args[4] = config;
    args[5] = "--userdatadir"; args[6] = profile;
    for (int i = 1; i < argc; ++i) args[i + 6] = argv[i];
    execv(binary, args);
    perror("Cannot start Retrocycles RCL");
    free(args);
    return 1;
}
