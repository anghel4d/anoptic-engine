/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef VULKANCONFIG_H
#define VULKANCONFIG_H

#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"


/* Write Functions */

// Preferred GPU by name; matched at instance init.
bool requestDevice(const char* deviceName);

// Prefer this present mode when available at swapchain create.
bool requestPresentMode(VkPresentModeKHR presentMode);

// Preferred MSAA samples (2/4/8). Clamped at init; <2 raised to 2. !TODO before initVulkan
bool requestMsaaSamples(uint32_t samples);

bool setResolution(Dimensions2D dimensions);

bool setMonitor(uint32_t index);

bool setBorderless(bool borderless);

// !TODO must be set before initVulkan
bool setVulkanDebug(bool debug);

/* Read Functions */

const char* getChosenDevice();

VkPresentModeKHR getChosenPresentMode();

uint32_t getChosenMsaaSamples();

Dimensions2D getChosenResolution();

uint32_t getChosenMonitor();

bool getChosenBorderless();

/* Active Functions */

bool updateWindow(GLFWwindow *window);


#endif
