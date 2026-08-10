// Per-scope Vulkan extension contracts 〜 requirement tags drive the support-check and
// enable loops in device.c (device scope) and instance.c (instance scope).

#ifndef ANOPTIC_VK_EXTENSION_CONTRACT_H
#define ANOPTIC_VK_EXTENSION_CONTRACT_H

#include <stdint.h>

enum class AnoVkExtensionRule : uint8_t {
	required,           // suitability-checked; always enabled
	implied_by_feature, // enabled only alongside its enabling feature (caller supplies the gate)
	debug_build,        // enabled in DEBUG_BUILD only
	apple_platform,     // enabled on __APPLE__ only; never support-checked
};

struct AnoVkExtensionContract final {
	const char* name;
	AnoVkExtensionRule rule;
};

// Build-time activation for platform/build tags; feature-implied entries gate at the call site.
constexpr bool ano_vk_extension_active(AnoVkExtensionRule rule)
{
	switch (rule) {
	case AnoVkExtensionRule::required:
	case AnoVkExtensionRule::implied_by_feature:
		return true;
	case AnoVkExtensionRule::debug_build:
#ifdef DEBUG_BUILD
		return true;
#else
		return false;
#endif
	case AnoVkExtensionRule::apple_platform:
#ifdef __APPLE__
		return true;
#else
		return false;
#endif
	}
	return false;
}

#endif
