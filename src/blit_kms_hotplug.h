#ifndef BLIT_KMS_HOTPLUG_H
#define BLIT_KMS_HOTPLUG_H

/* ADR0130. Read-only connector snapshots are produced off the frame loop.
   Scanout, output slots and topology remain owned by the main thread. */
static int kms_read_snapshot(BlitKms *kms, KmsSnapshot *snapshot)
{
    drmModeRes *resources = drmModeGetResources(kms->fd);
    int i;
    memset(snapshot, 0, sizeof(*snapshot));
    if (resources == NULL) return -1;
    if (resources->count_connectors < 0 || resources->count_connectors > 128) goto failed;
    for (i = 0; i < resources->count_connectors; ++i) {
        drmModeConnector *connector = drmModeGetConnector(kms->fd, resources->connectors[i]);
        KmsConnector *stored;
        uint32_t possible = 0;
        int e, c;
        if (connector == NULL) goto failed;
        if (connector->connection == DRM_MODE_UNKNOWNCONNECTION) {
            if (snapshot->unknown_count >= KMS_MAX_OUTPUTS) { drmModeFreeConnector(connector); goto failed; }
            snapshot->unknown[snapshot->unknown_count++] = connector->connector_id;
            drmModeFreeConnector(connector);
            continue;
        }
        if (connector->connection != DRM_MODE_CONNECTED || connector->count_modes < 1) {
            drmModeFreeConnector(connector);
            continue;
        }
        if (snapshot->count >= KMS_MAX_OUTPUTS) {
            drmModeFreeConnector(connector);
            goto failed;
        }
        stored = &snapshot->connectors[snapshot->count++];
        stored->id = connector->connector_id;
        snprintf(stored->name, sizeof(stored->name), "%s-%u",
                 drmModeGetConnectorTypeName(connector->connector_type) ?
                    drmModeGetConnectorTypeName(connector->connector_type) : "Unknown", connector->connector_type_id);
        stored->mode_count = connector->count_modes < KMS_MAX_MODES ? connector->count_modes : KMS_MAX_MODES;
        memcpy(stored->modes, connector->modes, stored->mode_count * sizeof(stored->modes[0]));
        for (e = -1; e < connector->count_encoders; ++e) {
            uint32_t id = e < 0 ? connector->encoder_id : connector->encoders[e];
            drmModeEncoder *encoder;
            if (!id) continue;
            encoder = drmModeGetEncoder(kms->fd, id);
            if (encoder == NULL) { drmModeFreeConnector(connector); goto failed; }
            possible |= encoder->possible_crtcs;
            drmModeFreeEncoder(encoder);
        }
        for (c = 0; c < resources->count_crtcs && c < 32; ++c)
            if ((possible & (1U << c)) && stored->crtc_count < KMS_MAX_OUTPUTS)
                stored->crtcs[stored->crtc_count++] = resources->crtcs[c];
        drmModeFreeConnector(connector);
    }
    snapshot->valid = 1;
    drmModeFreeResources(resources);
    return 0;
failed:
    drmModeFreeResources(resources);
    return -1;
}

static int kms_release_output(BlitKms *kms, KmsOutput *output, int restore)
{
    int result;
    if (!output->allocated) return 0;
    if (restore && output->old_crtc != NULL && output->old_crtc->mode_valid)
        result = drmModeSetCrtc(kms->fd, output->old_crtc->crtc_id,
            output->old_crtc->buffer_id, output->old_crtc->x,
            output->old_crtc->y, &output->connector, 1, &output->old_crtc->mode);
    else
        result = drmModeSetCrtc(kms->fd, output->crtc, 0, 0, 0, NULL, 0, NULL);
    if (result < 0) {
        output->info.presentation_state = BLIT_KMS_PRESENTATION_FAILED;
        snprintf(kms->hotplug_error, sizeof(kms->hotplug_error), "connector_%u_disable_failed", output->connector);
        return -1;
    }
    /* Disable/restore quiesces scanout. A queued event may still arrive, so
       invalidate its immutable cookie before this stable slot can be reused. */
    output->generation = ++kms->generation;
    output->flip_pending = 0;
    free_buffer(kms->fd, &output->front);
    free_buffer(kms->fd, &output->back);
    free(output->scene);
    output->scene = NULL;
    output->scene_size = 0;
    if (output->old_crtc != NULL) drmModeFreeCrtc(output->old_crtc);
    output->old_crtc = NULL;
    output->allocated = 0;
    return 0;
}

static int kms_activate_output(BlitKms *kms, const KmsConnector *connector)
{
    KmsOutput *output = NULL;
    drmModeConnector modes;
    int slot = -1, i, c, mode_index, x = 0, y = 0;
    uint32_t crtc = 0;
    for (i = 0; i < KMS_MAX_OUTPUTS; ++i)
        if (!kms->outputs[i].allocated && kms->outputs[i].connector == connector->id) { slot = i; break; }
    if (slot < 0)
        for (i = 0; i < KMS_MAX_OUTPUTS; ++i)
            if (!kms->outputs[i].allocated) { slot = i; break; }
    if (slot < 0 || kms->output_count >= KMS_MAX_OUTPUTS) goto failed;
    for (c = 0; c < connector->crtc_count; ++c) {
        int used = 0;
        for (i = 0; i < KMS_MAX_OUTPUTS; ++i)
            if (kms->outputs[i].allocated && kms->outputs[i].crtc == connector->crtcs[c]) used = 1;
        if (!used) { crtc = connector->crtcs[c]; break; }
    }
    if (!crtc) goto failed;
    output = &kms->outputs[slot];
    if (output->connector == connector->id) { x = output->info.x; y = output->info.y; }
    else
        for (i = 0; i < kms->output_count; ++i) {
            KmsOutput *prior = kms_output(kms, i);
            if (prior->info.x + prior->info.width > x) x = prior->info.x + prior->info.width;
        }
    memset(output, 0, sizeof(*output));
    output->generation = ++kms->generation;
    output->connector = connector->id;
    output->crtc = crtc;
    output->font = kms->font;
    memset(&modes, 0, sizeof(modes));
    modes.count_modes = connector->mode_count;
    modes.modes = (drmModeModeInfo *)connector->modes;
    mode_index = choose_mode(&modes);
    output->mode = connector->modes[mode_index];
    output->mode_count = connector->mode_count;
    output->active_mode = mode_index;
    for (i = 0; i < connector->mode_count; ++i) {
        output->modes[i].width = connector->modes[i].hdisplay;
        output->modes[i].height = connector->modes[i].vdisplay;
        output->modes[i].refresh_hz = mode_refresh_hz(&connector->modes[i]);
        output->modes[i].preferred = (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) != 0;
    }
    output->info.id = connector->id;
    snprintf(output->info.name, sizeof(output->info.name), "KMS connector %u", connector->id);
    snprintf(output->info.connector, sizeof(output->info.connector), "%s", connector->name);
    output->info.x = x; output->info.y = y;
    output->info.width = output->mode.hdisplay; output->info.height = output->mode.vdisplay;
    output->info.refresh_hz = mode_refresh_hz(&output->mode);
    output->info.mode_count = output->mode_count; output->info.active_mode = mode_index;
    memcpy(output->info.modes, output->modes, sizeof(output->modes));
    output->info.presentation_state = BLIT_KMS_PRESENTATION_PRESENTING;
    output->old_crtc = drmModeGetCrtc(kms->fd, crtc);
    if (output->old_crtc == NULL || make_buffer(kms->fd, output, &output->front) < 0 ||
        make_buffer(kms->fd, output, &output->back) < 0) goto failed;
    if (drmModeSetCrtc(kms->fd, crtc, output->front.fb, 0, 0,
                      &output->connector, 1, &output->mode) < 0) goto failed;
    output->allocated = 1;
    kms->slots[kms->output_count++] = slot;
    fprintf(stderr, "blit: KMS output added connector=%s id=%u mode=%ux%u\n",
            connector->name, connector->id, output->mode.hdisplay, output->mode.vdisplay);
    return 0;
failed:
    if (output != NULL) {
        free_buffer(kms->fd, &output->front); free_buffer(kms->fd, &output->back);
        if (output->old_crtc != NULL) drmModeFreeCrtc(output->old_crtc);
        output->old_crtc = NULL;
    }
    snprintf(kms->hotplug_error, sizeof(kms->hotplug_error), "connector_%u_activation_failed", connector->id);
    return -1;
}

static int kms_reconcile_snapshot(BlitKms *kms, const KmsSnapshot *snapshot)
{
    int i, j, changed = 0;
    if (!snapshot->valid) {
        snprintf(kms->hotplug_error, sizeof(kms->hotplug_error), "connector_snapshot_unavailable_or_capacity_exceeded");
        return 0;
    }
    kms->hotplug_error[0] = '\0';
    if (snapshot->unknown_count)
        snprintf(kms->hotplug_error, sizeof(kms->hotplug_error), "connector_state_unknown_inventory_retained");
    /* Retry quarantined teardown without ever assigning its CRTC elsewhere. */
    for (i = 0; i < KMS_MAX_OUTPUTS; ++i) {
        int listed = 0;
        for (j = 0; j < kms->output_count; ++j) if (kms->slots[j] == i) listed = 1;
        if (kms->outputs[i].allocated && !listed) kms_release_output(kms, &kms->outputs[i], 0);
    }
    for (i = 0; i < kms->output_count;) {
        KmsOutput *output = kms_output(kms, i);
        int keep = 0, uncertain = 0;
        for (j = 0; j < snapshot->unknown_count; ++j)
            if (snapshot->unknown[j] == output->connector) uncertain = 1;
        if (uncertain) { ++i; continue; }
        for (j = 0; j < snapshot->count; ++j)
            if (snapshot->connectors[j].id == output->connector) {
                int m;
                for (m = 0; m < snapshot->connectors[j].mode_count; ++m)
                    if (memcmp(&snapshot->connectors[j].modes[m], &output->mode, sizeof(output->mode)) == 0) keep = 1;
            }
        if (keep && output->info.presentation_state != BLIT_KMS_PRESENTATION_FAILED) { ++i; continue; }
        kms_release_output(kms, output, 0);
        memmove(&kms->slots[i], &kms->slots[i + 1], (kms->output_count - i - 1) * sizeof(kms->slots[0]));
        --kms->output_count; changed = 1;
    }
    for (i = 0; i < snapshot->count; ++i) {
        int found = 0;
        for (j = 0; j < kms->output_count; ++j)
            if (kms_output(kms, j)->connector == snapshot->connectors[i].id) found = 1;
        if (!found && kms_activate_output(kms, &snapshot->connectors[i]) == 0) changed = 1;
    }
    if (changed) ++kms->inventory_revision;
    return changed;
}

static void *kms_monitor(void *argument)
{
    BlitKms *kms = argument;
    struct pollfd fds[2];
    for (;;) {
        KmsSnapshot snapshot;
        long long started;
        int result;
        fds[0].fd = kms->monitor_stop[0]; fds[0].events = POLLIN;
        fds[1].fd = kms->monitor_socket; fds[1].events = POLLIN;
        result = poll(fds, 2, 5000);
        if (result < 0) { if (errno == EINTR) continue; break; }
        if (fds[0].revents) break;
        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            pthread_mutex_lock(&kms->monitor_lock);
            close(kms->monitor_socket); kms->monitor_socket = -1;
            pthread_mutex_unlock(&kms->monitor_lock);
        }
        if (fds[1].revents & POLLIN) {
            char message[4096];
            struct sockaddr_nl sender;
            socklen_t length = sizeof(sender);
            int received = recvfrom(kms->monitor_socket, message, sizeof(message), MSG_DONTWAIT,
                                    (struct sockaddr *)&sender, &length);
            if (received <= 0 || sender.nl_pid != 0 ||
                memmem(message, received, "SUBSYSTEM=drm", 13) == NULL) continue;
            /* Coalesce connector bursts on the worker, never the frame loop. */
            if (poll(fds, 1, 100) > 0) break;
            int drained;
            for (drained = 0; drained < 32; ++drained)
                if (recv(kms->monitor_socket, message, sizeof(message), MSG_DONTWAIT) <= 0) break;
        }
        started = kms_now_us();
        kms_read_snapshot(kms, &snapshot);
        pthread_mutex_lock(&kms->monitor_lock);
        kms->snapshot = snapshot;
        ++kms->snapshot_revision;
        ++kms->monitor_scans;
        kms->monitor_last_scan_ms = (double)(kms_now_us() - started) / 1000.0;
        pthread_mutex_unlock(&kms->monitor_lock);
    }
    return NULL;
}

static int kms_start_monitor(BlitKms *kms)
{
    struct sockaddr_nl address;
    if (pipe(kms->monitor_stop) < 0) return -1;
    fcntl(kms->monitor_stop[0], F_SETFD, FD_CLOEXEC);
    fcntl(kms->monitor_stop[1], F_SETFD, FD_CLOEXEC);
    kms->monitor_socket = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (kms->monitor_socket >= 0) {
        memset(&address, 0, sizeof(address)); address.nl_family = AF_NETLINK; address.nl_groups = 1;
        if (bind(kms->monitor_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
            close(kms->monitor_socket); kms->monitor_socket = -1;
        }
    }
    if (pthread_create(&kms->monitor_thread, NULL, kms_monitor, kms) != 0) return -1;
    kms->monitor_started = 1;
    return 0;
}

#endif
