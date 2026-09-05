#include <stdio.h>
#include <string.h>
#include "diagnostics/gateway_diagnostics.h"

static int lines_bounded(const char *text)
{
    size_t length = 0;
    while (*text != '\0') {
        if (*text++ == '\n') length = 0;
        else if (++length >= GATEWAY_DIAGNOSTIC_LINE_MAX) return 0;
    }
    return 1;
}

int main(void)
{
    gateway_diagnostic_snapshot_t snapshot;
    char output[GATEWAY_DIAGNOSTIC_SUMMARY_MAX + 32U];
    size_t used, i;
    memset(&snapshot, 0, sizeof(snapshot));
    memset(snapshot.application.configuration_directory, 'x',
           sizeof(snapshot.application.configuration_directory) - 1U);
    memset(output, 0x5a, sizeof(output));
    if (gateway_diagnostics_render_configuration(&snapshot, output,
                                                  sizeof(output), &used) !=
        GATEWAY_RENDER_OK || !lines_bounded(output)) return 1;
    for (i = GATEWAY_DIAGNOSTIC_SUMMARY_MAX; i < sizeof(output); ++i)
        if ((unsigned char)output[i] != 0x5aU) return 1;
    printf("gateway_diagnostic_bounds checks=2 failed=0 line_max=%u summary_max=%u\n",
           GATEWAY_DIAGNOSTIC_LINE_MAX, GATEWAY_DIAGNOSTIC_SUMMARY_MAX);
    return 0;
}
