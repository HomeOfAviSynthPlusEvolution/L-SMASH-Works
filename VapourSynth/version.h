#define VERSION "<output of git describe --tags --long>"

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
static const char *config_opts = "-Dcachedir=\"" STRINGIFY(DEFAULT_CACHEDIR) "\"";
