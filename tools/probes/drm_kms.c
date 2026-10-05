/*
 * blit DRM/KMS hardware boundary probe.
 *
 * This is diagnostic code only. It does not become a second renderer. Its
 * purpose is to establish the exact card/connector/mode inventory that the
 * production KMS backend will consume.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static void print_connector(int fd, drmModeConnector *connector)
{
    int i;

    printf("    connector=%u name=%s status=%s modes=%d\n",
           connector->connector_id,
           connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort ?
               "DisplayPort" :
           connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ?
               "HDMI-A" : "other",
           connector->connection == DRM_MODE_CONNECTED ? "connected" :
               connector->connection == DRM_MODE_UNKNOWNCONNECTION ?
                   "unknown" : "disconnected",
           connector->count_modes);
    for (i = 0; i < connector->count_modes; ++i) {
        drmModeModeInfo *mode = &connector->modes[i];
        printf("      mode=%s %ux%u@%u preferred=%s\n",
               mode->name, mode->hdisplay, mode->vdisplay,
               mode->vrefresh,
               (mode->type & DRM_MODE_TYPE_PREFERRED) ? "true" : "false");
    }
}

int main(void)
{
    int card;
    int found = 0;

    for (card = 0; card < 16; ++card) {
        char path[32];
        int fd;
        drmModeRes *resources;
        int i;

        snprintf(path, sizeof(path), "/dev/dri/card%d", card);
        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        resources = drmModeGetResources(fd);
        if (resources == NULL) {
            fprintf(stderr, "%s: cannot query resources: %s\n", path,
                    strerror(errno));
            close(fd);
            continue;
        }
        ++found;
        printf("card=%s fd=%d connectors=%d crtcs=%d\n", path, fd,
               resources->count_connectors, resources->count_crtcs);
        for (i = 0; i < resources->count_connectors; ++i) {
            drmModeConnector *connector = drmModeGetConnector(
                fd, resources->connectors[i]);
            if (connector == NULL)
                continue;
            print_connector(fd, connector);
            drmModeFreeConnector(connector);
        }
        drmModeFreeResources(resources);
        close(fd);
    }
    if (found == 0) {
        fprintf(stderr, "no usable DRM cards found\n");
        return 1;
    }
    return 0;
}
