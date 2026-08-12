#include <stdio.h>
#include <stdbool.h>

#include "vulkan_backend/vulkanMaster.h"

extern bool g_AnoVkNoSuitableGpu;

int main() {
    const char *capturePath = "anotest_vk_lifecycle.ppm";
    remove(capturePath);
    printf("Starting Vulkan lifecycle test...\n");
    bool result = initVulkan(nullptr);
    if (!result) {
        if (g_AnoVkNoSuitableGpu) {
            printf("SKIP: no Vulkan device here can run the renderer.\n");
            return 77; // ctest SKIP_RETURN_CODE
        }
        fprintf(stderr, "initVulkan() failed.\n");
        return 1;
    }
    printf("initVulkan() succeeded.\n");
    if (ano_render_capture_next_frame(NULL)) {
        fprintf(stderr, "capture accepted a null path.\n");
        unInitVulkan();
        return 1;
    }
    if (!ano_render_capture_next_frame(capturePath)) {
        fprintf(stderr, "renderer-native capture request failed.\n");
        unInitVulkan();
        return 1;
    }
    if (ano_render_capture_next_frame(capturePath)) {
        fprintf(stderr, "capture accepted a second in-flight request.\n");
        unInitVulkan();
        return 1;
    }
	// A pending request follows the next swapchain generation and is resized
	// before its image-to-buffer copy is recorded.
	rendererState.framebufferResized = true;
	drawFrame();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT + 1u; ++i)
        drawFrame();
    FILE *capture = fopen(capturePath, "rb");
    unsigned width = 0, height = 0, maximum = 0;
    char magic[3] = {};
    const bool captureValid = capture != NULL
        && fscanf(capture, "%2s %u %u %u", magic, &width, &height,
                  &maximum) == 4
        && magic[0] == 'P' && magic[1] == '6'
        && width != 0 && height != 0 && maximum == 255;
    if (capture != NULL)
        fclose(capture);
    remove(capturePath);
    if (!captureValid) {
        fprintf(stderr, "renderer-native capture was not a valid PPM.\n");
        unInitVulkan();
        return 1;
    }
    unInitVulkan();
    printf("unInitVulkan() completed.\n");
    return 0;
}
