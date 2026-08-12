/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <anoptic_memory_typed.h>
#include <anoptic_log.h>
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/text_raster.h"

#include <anoptic_meta.h>
#include "extension_contract.h"

// Hard-required device extensions. VK_EXT_mesh_shader appended dynamically when supported.
inline constexpr AnoVkExtensionContract ANO_VK_DEVICE_EXTENSIONS[] = { // Make dynamic, determined at runtime
	{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, AnoVkExtensionRule::required },
	{ VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, AnoVkExtensionRule::required },
	{ VK_EXT_MESH_SHADER_EXTENSION_NAME, AnoVkExtensionRule::implied_by_feature },
	// VK_KHR_portability_subset MUST be enabled when exposed. String literal avoids vulkan_beta.h.
	{ "VK_KHR_portability_subset", AnoVkExtensionRule::apple_platform },
};

// Feature participation bits 〜 one policy field per Vk feature member the engine touches.
enum : uint8_t {
	ANO_VK_FEATURE_QUERIED    = 1u << 0, // read into DeviceCapabilities by populateCapabilities
	ANO_VK_FEATURE_REQUIRED   = 1u << 1, // gates isDeviceSuitable; enabled at device creation
	ANO_VK_FEATURE_ENABLED    = 1u << 2, // enabled at device creation when the device reports it
	ANO_VK_FEATURE_FORCE_OFF  = 1u << 3, // pinned VK_FALSE at device creation
	ANO_VK_FEATURE_QUERY_ONLY = 1u << 4, // deliberately queried without ever enabling
};

struct AnoVkFeatureRule final { uint8_t flags; };

// Engine-owned feature policy 〜 field identifiers name members of the foreign
// VkPhysicalDevice*Features struct; a consteval pass resolves each by identifier and splices
// the query/check/enable accesses. Mesh chaining, core-promotion handling, env overrides,
// and the vertex fallback stay explicit in the functions below.
struct AnoVkCoreFeaturePolicy final { // VkPhysicalDeviceFeatures
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED | ANO_VK_FEATURE_QUERIED }]] VkBool32 shaderInt64;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED }]] VkBool32 samplerAnisotropy;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED }]] VkBool32 multiDrawIndirect;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED | ANO_VK_FEATURE_QUERIED }]] VkBool32 shaderFloat64;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED }]] VkBool32 geometryShader;
	// Vertex fallback needs drawIndirectFirstInstance.
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED }]] VkBool32 drawIndirectFirstInstance;
	// independentBlend for per-attachment blend.
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED }]] VkBool32 independentBlend;
};

struct AnoVk12FeaturePolicy final { // VkPhysicalDeviceVulkan12Features
	// Required Vulkan 1.2 descriptor features.
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED }]] VkBool32 descriptorIndexing;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED }]] VkBool32 shaderSampledImageArrayNonUniformIndexing;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED }]] VkBool32 runtimeDescriptorArray;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED }]] VkBool32 descriptorBindingPartiallyBound;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED }]] VkBool32 descriptorBindingVariableDescriptorCount;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED }]] VkBool32 descriptorBindingSampledImageUpdateAfterBind;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED | ANO_VK_FEATURE_QUERIED }]] VkBool32 drawIndirectCount;
	// gl_Layer + viewport index for layered shadow blur.
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED | ANO_VK_FEATURE_QUERIED }]] VkBool32 shaderOutputLayer;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED | ANO_VK_FEATURE_QUERIED }]] VkBool32 shaderOutputViewportIndex;
	// Async Hi-Z ordering.
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED | ANO_VK_FEATURE_QUERIED }]] VkBool32 timelineSemaphore;
	// fp16 CDF reconstruct when present.
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED | ANO_VK_FEATURE_QUERIED }]] VkBool32 shaderFloat16;
};

struct AnoVk11FeaturePolicy final { // VkPhysicalDeviceVulkan11Features
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED }]] VkBool32 shaderDrawParameters; // flat.vert uses gl_DrawID -> SPIR-V DrawParameters cap
};

struct AnoVkDynamicRenderingFeaturePolicy final { // VkPhysicalDeviceDynamicRenderingFeaturesKHR
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_REQUIRED }]] VkBool32 dynamicRendering;
};

struct AnoVkMeshFeaturePolicy final { // VkPhysicalDeviceMeshShaderFeaturesEXT
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED | ANO_VK_FEATURE_QUERIED }]] VkBool32 taskShader;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED | ANO_VK_FEATURE_QUERIED }]] VkBool32 meshShader;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_FORCE_OFF }]] VkBool32 multiviewMeshShader;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_FORCE_OFF }]] VkBool32 primitiveFragmentShadingRateMeshShader;
	[[=AnoVkFeatureRule{ ANO_VK_FEATURE_ENABLED }]] VkBool32 meshShaderQueries;
};

// Single AnoVkFeatureRule annotation on a policy field.
static consteval AnoVkFeatureRule ano_vk_field_rule(std::meta::info field)
{
	auto rules = std::meta::annotations_of_with_type(field, ^^AnoVkFeatureRule);
	if (rules.size() != 1)
		__builtin_abort();
	return std::meta::extract<AnoVkFeatureRule>(rules[0]);
}

// Count of Foreign members whose identifier equals name.
template<class Foreign>
static consteval size_t ano_vk_feature_matches(std::string_view name)
{
	size_t matches = 0;
	for (std::meta::info member : std::meta::nonstatic_data_members_of(
			^^Foreign, std::meta::access_context::current()))
		if (std::meta::has_identifier(member) && std::meta::identifier_of(member) == name)
			++matches;
	return matches;
}

// The Foreign member named name; resolution is validated by ano_vk_validate_feature_policy.
static consteval std::meta::info ano_vk_feature_member(std::meta::info foreign, std::string_view name)
{
	for (std::meta::info member : std::meta::nonstatic_data_members_of(
			foreign, std::meta::access_context::current()))
		if (std::meta::has_identifier(member) && std::meta::identifier_of(member) == name)
			return member;
	__builtin_abort();
}

// Policy contract: every field resolves to exactly one foreign member, carries exactly one
// role, and a queried field must be required, enabled, or explicitly query-only.
template<class Policy, class Foreign>
static consteval bool ano_vk_validate_feature_policy()
{
	static constexpr auto fields = std::define_static_array(
		std::meta::nonstatic_data_members_of(^^Policy, std::meta::access_context::current()));
	template for (constexpr auto field : fields) {
		static_assert(ano_vk_feature_matches<Foreign>(std::meta::identifier_of(field)) == 1,
			"feature policy field does not name exactly one foreign Vk feature member");
		constexpr AnoVkFeatureRule rule = ano_vk_field_rule(field);
		static_assert(!(rule.flags & ANO_VK_FEATURE_QUERIED)
			|| (rule.flags & (ANO_VK_FEATURE_REQUIRED | ANO_VK_FEATURE_ENABLED | ANO_VK_FEATURE_QUERY_ONLY)),
			"queried feature is never checked or enabled 〜 tag ANO_VK_FEATURE_QUERY_ONLY if deliberate");
		constexpr uint8_t role = rule.flags & (ANO_VK_FEATURE_REQUIRED | ANO_VK_FEATURE_ENABLED
			| ANO_VK_FEATURE_FORCE_OFF | ANO_VK_FEATURE_QUERY_ONLY);
		static_assert(role != 0 && (role & (role - 1)) == 0,
			"feature policy field needs exactly one role: REQUIRED, ENABLED, FORCE_OFF, or QUERY_ONLY");
		static_assert(!(rule.flags & ANO_VK_FEATURE_QUERY_ONLY) || (rule.flags & ANO_VK_FEATURE_QUERIED),
			"query-only feature must also be tagged QUERIED");
	}
	return true;
}

static_assert(ano_vk_validate_feature_policy<AnoVkCoreFeaturePolicy, VkPhysicalDeviceFeatures>());
static_assert(ano_vk_validate_feature_policy<AnoVk12FeaturePolicy, VkPhysicalDeviceVulkan12Features>());
static_assert(ano_vk_validate_feature_policy<AnoVk11FeaturePolicy, VkPhysicalDeviceVulkan11Features>());
static_assert(ano_vk_validate_feature_policy<AnoVkDynamicRenderingFeaturePolicy, VkPhysicalDeviceDynamicRenderingFeaturesKHR>());
static_assert(ano_vk_validate_feature_policy<AnoVkMeshFeaturePolicy, VkPhysicalDeviceMeshShaderFeaturesEXT>());

// Enable pass 〜 copies REQUIRED and ENABLED members from the queried chain; FORCE_OFF pinned VK_FALSE.
template<class Policy, class Foreign>
static void ano_vk_apply_features(Foreign& enable, const Foreign& query)
{
	static constexpr auto fields = std::define_static_array(
		std::meta::nonstatic_data_members_of(^^Policy, std::meta::access_context::current()));
	template for (constexpr auto field : fields) {
		constexpr AnoVkFeatureRule rule = ano_vk_field_rule(field);
		constexpr auto member = ano_vk_feature_member(^^Foreign, std::meta::identifier_of(field));
		if constexpr (rule.flags & (ANO_VK_FEATURE_REQUIRED | ANO_VK_FEATURE_ENABLED))
			enable.[:member:] = query.[:member:];
		else if constexpr (rule.flags & ANO_VK_FEATURE_FORCE_OFF)
			enable.[:member:] = VK_FALSE;
	}
}

// Suitability pass 〜 conjunction of members the policy marks REQUIRED.
template<class Policy, class Foreign>
static bool ano_vk_required_features_present(const Foreign& query)
{
	bool present = true;
	static constexpr auto fields = std::define_static_array(
		std::meta::nonstatic_data_members_of(^^Policy, std::meta::access_context::current()));
	template for (constexpr auto field : fields) {
		if constexpr (ano_vk_field_rule(field).flags & ANO_VK_FEATURE_REQUIRED)
			present = present && query.[:ano_vk_feature_member(^^Foreign, std::meta::identifier_of(field)):];
	}
	return present;
}

// Query splice 〜 the policy field named name must carry QUERIED; yields the foreign member.
template<class Policy, class Foreign>
static consteval std::meta::info ano_vk_queried(std::string_view name)
{
	for (std::meta::info field : std::meta::nonstatic_data_members_of(
			^^Policy, std::meta::access_context::current()))
		if (std::meta::identifier_of(field) == name) {
			if (!(ano_vk_field_rule(field).flags & ANO_VK_FEATURE_QUERIED))
				__builtin_abort();
			return ano_vk_feature_member(^^Foreign, name);
		}
	__builtin_abort();
}

static consteval std::meta::info ano_vk_q_core(std::string_view name)
{ return ano_vk_queried<AnoVkCoreFeaturePolicy, VkPhysicalDeviceFeatures>(name); }
static consteval std::meta::info ano_vk_q_12(std::string_view name)
{ return ano_vk_queried<AnoVk12FeaturePolicy, VkPhysicalDeviceVulkan12Features>(name); }
static consteval std::meta::info ano_vk_q_mesh(std::string_view name)
{ return ano_vk_queried<AnoVkMeshFeaturePolicy, VkPhysicalDeviceMeshShaderFeaturesEXT>(name); }

// VkPhysicalDeviceType is non-dense: the *_MAX_ENUM sentinel is excluded explicitly.
static consteval size_t ano_vk_device_type_count()
{
	size_t count = 0;
	for (std::meta::info enumerator : std::meta::enumerators_of(^^VkPhysicalDeviceType))
		if (!ano::enum_identifier_ends_with(std::meta::identifier_of(enumerator), "_MAX_ENUM"))
			++count;
	return count;
}

// Projection over the foreign enum: strip the VK_PHYSICAL_DEVICE_TYPE_ prefix and any
// _GPU suffix, lowercased 〜 reproduces the legacy name table exactly.
inline constexpr auto ANO_VK_DEVICE_TYPE_NAMES =
	ano::reflect_enum_values<VkPhysicalDeviceType, const char*, ano_vk_device_type_count(), 0>(
		[]<auto enumerator>() consteval {
			constexpr auto identifier = std::meta::identifier_of(enumerator);
			constexpr std::string_view prefix = "VK_PHYSICAL_DEVICE_TYPE_";
			static_assert(identifier.starts_with(prefix));
			std::string_view name = identifier.substr(prefix.size());
			if (ano::enum_identifier_ends_with(name, "_GPU"))
				name.remove_suffix(4);
			char lowered[identifier.size() + 1] = {};
			size_t length = 0;
			for (char value : name)
				lowered[length++] = ano::enum_name_case(value, ano::EnumNameCase::lower);
			return std::define_static_string(std::string_view(lowered, length));
		});

// Legacy table equivalence.
static_assert(ano::enum_name_equal(ANO_VK_DEVICE_TYPE_NAMES.values[VK_PHYSICAL_DEVICE_TYPE_OTHER], "other"));
static_assert(ano::enum_name_equal(ANO_VK_DEVICE_TYPE_NAMES.values[VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU], "integrated"));
static_assert(ano::enum_name_equal(ANO_VK_DEVICE_TYPE_NAMES.values[VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU], "discrete"));
static_assert(ano::enum_name_equal(ANO_VK_DEVICE_TYPE_NAMES.values[VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU], "virtual"));
static_assert(ano::enum_name_equal(ANO_VK_DEVICE_TYPE_NAMES.values[VK_PHYSICAL_DEVICE_TYPE_CPU], "cpu"));


struct QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR *surface) { // Extend with more queue family checks
	//!TODO add error-case returns and associated checking to all invoking functions
	struct QueueFamilyIndices indices = {};
	indices.graphicsPresent = false;
	indices.computePresent = false;
	indices.transferPresent = false;
	indices.presentPresent = false;

	indices.graphicsFamily = UINT32_MAX;
	indices.computeFamily = UINT32_MAX;
	indices.transferFamily = UINT32_MAX;
	indices.presentFamily = UINT32_MAX;
	
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
	if (queueFamilyCount == 0)
		return indices;
	VkQueueFamilyProperties* queueFamilies = ano::allocate<VkQueueFamilyProperties>(queueFamilyCount);
	if (queueFamilies == NULL)
		return indices;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

	// Select the first queue family satisfying each capability.
	//!TODO Implement these as required further into development
	// Prefer dedicated compute, else first compute-capable.
	bool haveDedicatedCompute = false;
	for (uint32_t i = 0; i < queueFamilyCount; i++)
	{	//Queue checks go here
		if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && indices.graphicsPresent == false)
		{
			indices.graphicsFamily = i;
			indices.graphicsPresent = true;
			//printf("Graphics: %d\n", i);
		}
		if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
		{
			bool dedicated = (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0;
			if (indices.computePresent == false || (dedicated && !haveDedicatedCompute))
			{
				indices.computeFamily = i;
				indices.computePresent = true;
				haveDedicatedCompute = dedicated;
				//printf("Compute: %d\n", i);
			}
		}
		if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && indices.transferPresent == false)
		{
			indices.transferFamily = i;
			indices.transferPresent = true;
			//printf("Transfer: %d\n", i);
		}

		if (surface != NULL)
		{
			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, *surface, &presentSupport);	
			if (presentSupport) 
			{
				if (indices.presentPresent == false) // Primary present family, usually the graphics family
				{
					indices.presentFamily = i;	
				}
				indices.presentPresent = true;
				//printf("Present: %d\n", indices.presentFamily);
			}
		}
		else
		{
			//printf("Surface invalid in queue selection!\n");
		}
		//printf("Queue family %d flags: %d\n", i, queueFamilies[i].queueFlags);
	}
	//printf("Final output:\n  Graphics Family: %d\n  Compute family: %d
	// \n  Transfer Family: %d\n  Present Family: %d\n\n",
	// indices.graphicsFamily, indices.computeFamily, indices.transferFamily, indices.presentFamily);
	free(queueFamilies);
	return indices;
}

struct DeviceCapabilities populateCapabilities(VkPhysicalDevice device) // Select required capabilities, extend with checks and error states
{
	struct DeviceCapabilities capabilities;

	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {};
	meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.pNext = &meshShaderFeatures;

	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &features12;

	vkGetPhysicalDeviceFeatures2(device, &features2);

	//Device features checks
	capabilities.float64 = features2.features.[:ano_vk_q_core("shaderFloat64"):];
	capabilities.int64 = features2.features.[:ano_vk_q_core("shaderInt64"):];
	capabilities.drawIndirectCount = features12.[:ano_vk_q_12("drawIndirectCount"):];
	// Mesh shader when the extension feature is usable.
	capabilities.meshShader = meshShaderFeatures.[:ano_vk_q_mesh("meshShader"):];
	// ANO_FORCE_NO_MESH_SHADER: force vertex fallback.
	if (getenv("ANO_FORCE_NO_MESH_SHADER")) capabilities.meshShader = false;
	// Task stage for per-meshlet cull, mesh path only.
	capabilities.taskShader = capabilities.meshShader && meshShaderFeatures.[:ano_vk_q_mesh("taskShader"):];

	// Depth-resolve MAX for Hi-Z farthest sample.
	VkPhysicalDeviceDepthStencilResolveProperties dsResolve = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES };
	VkPhysicalDeviceProperties2 props2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &dsResolve };
	vkGetPhysicalDeviceProperties2(device, &props2);
	capabilities.depthMaxResolve = (dsResolve.supportedDepthResolveModes & VK_RESOLVE_MODE_MAX_BIT) != 0;
	if (getenv("ANO_FORCE_NO_DEPTH_RESOLVE")) capabilities.depthMaxResolve = false;

	// gl_Layer + viewport index for layered shadow blur.
	capabilities.shaderOutputLayer = features12.[:ano_vk_q_12("shaderOutputLayer"):] && features12.[:ano_vk_q_12("shaderOutputViewportIndex"):];
	if (getenv("ANO_FORCE_NO_SHADER_OUTPUT_LAYER")) capabilities.shaderOutputLayer = false;

	// Timeline semaphores for cross-queue ordering in the async Hi-Z build.
	capabilities.timelineSemaphore = features12.[:ano_vk_q_12("timelineSemaphore"):];

	// fp16 arithmetic selects the *_fp16.frag lighting variants.
	capabilities.shaderFloat16 = features12.[:ano_vk_q_12("shaderFloat16"):];
	if (getenv("ANO_FORCE_NO_FP16")) capabilities.shaderFloat16 = false;
	ano::log(ano::Info, "CDF reconstruct: %s", capabilities.shaderFloat16 ? "fp16" : "fp32 (no shaderFloat16)");

	//Queue family checks
	struct QueueFamilyIndices indices = findQueueFamilies(device, NULL);
	capabilities.graphics = indices.graphicsPresent;
	capabilities.compute = indices.computePresent;
	capabilities.transfer = indices.transferPresent;
	return capabilities;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device) { // Rework extensions system, add a definition interface
	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);

	VkExtensionProperties* availableExtensions = (VkExtensionProperties*) calloc(1, extensionCount * sizeof(VkExtensionProperties));
	vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);

	for (const AnoVkExtensionContract& contract : ANO_VK_DEVICE_EXTENSIONS)
	{
		if (contract.rule != AnoVkExtensionRule::required)
			continue;
		bool found = false;
		for (uint32_t j = 0; j < extensionCount; ++j)
		{
			if (strcmp(contract.name, availableExtensions[j].extensionName) == 0)
			{
				found = true;
				break;
			}
		}

		if (!found)
		{
			free(availableExtensions);
			return false; // Required extension missing
		}
	}

	free(availableExtensions);
	return true; // All found
}

bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR *surface) // Extend and integrate with capability checks, expose via interface
{
	struct QueueFamilyIndices indices = findQueueFamilies(device, surface);
	bool extensionsSupported = checkDeviceExtensionSupport(device);
	bool queueRequirements = indices.graphicsPresent;

	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {};
	meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

	VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeature = {};
	dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
	dynamicRenderingFeature.pNext = &meshShaderFeatures;

	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.pNext = &dynamicRenderingFeature;

	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &features12;

	vkGetPhysicalDeviceFeatures2(device, &features2);

	// geometryShader / shaderFloat64 not required.
	bool physicalRequirements = ano_vk_required_features_present<AnoVkCoreFeaturePolicy>(features2.features);

	bool requiredFeatures12 = ano_vk_required_features_present<AnoVk12FeaturePolicy>(features12);
	bool requiredDynamicRendering = ano_vk_required_features_present<AnoVkDynamicRenderingFeaturePolicy>(dynamicRenderingFeature);
	bool requiredMultiDraw = features2.features.multiDrawIndirect;

	// Mesh shader optional; vertex fallback exists.
	if (!requiredFeatures12 || !requiredDynamicRendering || !requiredMultiDraw) {
		ano::log(ano::Warn, "Device rejected: lacks required Vulkan 1.2, dynamic rendering, or multiDrawIndirect features.");
		return false;
	}
	if (!meshShaderFeatures.meshShader) {
		ano::log(ano::Warn, "Device lacks VK_EXT_mesh_shader: will use the vertex-shader fallback path.");
		// Vertex fallback needs drawIndirectFirstInstance.
		if (!features2.features.drawIndirectFirstInstance) {
			ano::log(ano::Warn, "Device rejected: also lacks drawIndirectFirstInstance, so the vertex "
			             "fallback path cannot draw correctly.");
			return false;
		}
	}

	// 1x-only sample support cannot render. Mirrors getMaxUsableSampleCount.
	VkPhysicalDeviceVulkan12Properties vk12Props = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
	VkPhysicalDeviceProperties2 props2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &vk12Props };
	vkGetPhysicalDeviceProperties2(device, &props2);
	VkSampleCountFlags usableCounts = props2.properties.limits.framebufferColorSampleCounts
	                                & props2.properties.limits.framebufferDepthSampleCounts
	                                & props2.properties.limits.sampledImageDepthSampleCounts
	                                & vk12Props.framebufferIntegerColorSampleCounts;
	if (!(usableCounts & ~(VkSampleCountFlags)VK_SAMPLE_COUNT_1_BIT)) {
		ano::log(ano::Warn, "Device rejected: supports only 1x MSAA across the engine's attachment set, "
		             "and the renderer has no 1x path.");
		return false;
	}

	return physicalRequirements && queueRequirements && extensionsSupported;
}

VkSampleCountFlagBits getMaxUsableSampleCount(VulkanContext* ctx)
{
	// Integer color sample counts via Vulkan 1.2 properties.
	VkPhysicalDeviceVulkan12Properties vk12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
	VkPhysicalDeviceProperties2 physicalDeviceProperties2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &vk12 };
	vkGetPhysicalDeviceProperties2(ctx->physicalDevice, &physicalDeviceProperties2);
	VkPhysicalDeviceProperties physicalDeviceProperties = physicalDeviceProperties2.properties;

	// Also require sampled Hi-Z depth and R32_UINT picking samples.
	VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts
	                          & physicalDeviceProperties.limits.framebufferDepthSampleCounts
	                          & physicalDeviceProperties.limits.sampledImageDepthSampleCounts
	                          & vk12.framebufferIntegerColorSampleCounts;

	// Prefer configured MSAA (min 2x). ANO_MSAA overrides.
	uint32_t preferred = getChosenMsaaSamples();
	const char* msaaEnv = getenv("ANO_MSAA");
	if (msaaEnv) preferred = (uint32_t)atoi(msaaEnv);
	if (preferred < 2u) { ano::log(ano::Warn, "MSAA preference %u below minimum, using 2x", preferred); preferred = 2u; }
	VkSampleCountFlags mask = 0;
	for (uint32_t s = 2u; s <= preferred && s <= 64u; s <<= 1) mask |= s; // sample flags are their counts
	// Preference window empty, take any supported >=2x count.
	VkSampleCountFlags preferredCounts = counts & mask;
	counts = preferredCounts ? preferredCounts : (counts & ~(VkSampleCountFlags)VK_SAMPLE_COUNT_1_BIT);

	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

// Mesh-shader capability, the primary device-ranking key.
static bool deviceHasMeshShader(VkPhysicalDevice device)
{
	VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures = {};
	meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &meshFeatures;

	vkGetPhysicalDeviceFeatures2(device, &features2);
	return meshFeatures.meshShader;
}

// ASCII-only case fold. Non-ASCII bytes pass through unfolded.
static char asciiLower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// Caseless substring match for the ANO_DEVICE override.
static bool nameContainsCaseless(const char* haystack, const char* needle)
{
	size_t needleLen = strlen(needle);
	if (needleLen == 0)
		return false;
	for (; *haystack; haystack++)
	{
		size_t j = 0;
		while (j < needleLen && haystack[j] &&
		       asciiLower(haystack[j]) == asciiLower(needle[j]))
			j++;
		if (j == needleLen)
			return true;
	}
	return false;
}

// Largest DEVICE_LOCAL heap.
static VkDeviceSize maxDeviceLocalHeapSize(const VkPhysicalDeviceMemoryProperties* memProperties)
{
	VkDeviceSize maxSize = 0;
	for (uint32_t h = 0; h < memProperties->memoryHeapCount; h++)
	{
		if ((memProperties->memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) &&
		    memProperties->memoryHeaps[h].size > maxSize)
		{
			maxSize = memProperties->memoryHeaps[h].size;
		}
	}
	return maxSize;
}

bool pickPhysicalDevice(VulkanContext* ctx, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices, const char* preferredDevice) // Extend selection logic, split out device discovery, retain attributes for UI
{
	bool foundPreferredDevice = false;
	// ANO_DEVICE (caseless name substring) pins the adapter. Suitability checks still apply.
	const char* envDevice = getenv("ANO_DEVICE");
	ctx->deviceCount = 0;

	vkEnumeratePhysicalDevices(ctx->instance, &(ctx->deviceCount), NULL);

	if (ctx->deviceCount == 0) 
	{
		ano::log(ano::Fatal, "Failed to find GPUs with Vulkan support!");
		return false;
	}

	// Device names. Zeroed so free() on empty slots is a no-op.
	ctx->availableDevices = (char**)mi_calloc(ctx->deviceCount, sizeof(char*));

	VkPhysicalDevice* devices = (VkPhysicalDevice*)calloc(1, sizeof(VkPhysicalDevice) * ctx->deviceCount);

	vkEnumeratePhysicalDevices(ctx->instance, &ctx->deviceCount, devices);
	
	VkPhysicalDeviceProperties deviceProperties;
	// VkPhysicalDeviceFeatures deviceFeatures;
	VkPhysicalDeviceMemoryProperties memProperties;

	VkDeviceSize maxDedicatedMemory = 0;
	VkDeviceSize maxIntegratedMemory = 0;
	VkDeviceSize maxFallbackMemory = 0;

	VkPhysicalDevice bestDedicatedDevice = VK_NULL_HANDLE;
	VkPhysicalDevice bestIntegratedDevice = VK_NULL_HANDLE;
	// Fallback: CPU/virtual/other (lavapipe etc).
	VkPhysicalDevice bestFallbackDevice = VK_NULL_HANDLE;
	bool bestDedicatedMesh = false;
	bool bestIntegratedMesh = false;
	bool bestFallbackMesh = false;

	ano::log(ano::Info, "DeviceCount: %d", ctx->deviceCount);

	for (uint32_t i = 0; i < ctx->deviceCount; i++)
	{
		vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);
		vkGetPhysicalDeviceMemoryProperties(devices[i], &memProperties);
		// Log full device identity.
		ano::log(ano::Info, "Device %u: %s (%s, mesh shader: %s)", i, deviceProperties.deviceName,
		             (size_t)deviceProperties.deviceType < ANO_VK_DEVICE_TYPE_NAMES.count
		                 ? ANO_VK_DEVICE_TYPE_NAMES.values[deviceProperties.deviceType] : "unknown",
		             deviceHasMeshShader(devices[i]) ? "yes" : "no");

		if (isDeviceSuitable(devices[i], &(ctx->surface)))
		{

			// Select the first preferred device, if any.
			if (strcmp(deviceProperties.deviceName, preferredDevice) == 0 ||
			    (envDevice && nameContainsCaseless(deviceProperties.deviceName, envDevice)))
			{
				ctx->physicalDevice = devices[i];
				foundPreferredDevice = true;
				break;
			}

		
			// Rank suitable devices by mesh-shader capability first, then DEVICE_LOCAL memory.
			VkDeviceSize currentMemorySize = maxDeviceLocalHeapSize(&memProperties);
			bool currentMesh = deviceHasMeshShader(devices[i]);

			if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
			    ((currentMesh && !bestDedicatedMesh) ||
			     (currentMesh == bestDedicatedMesh && currentMemorySize > maxDedicatedMemory)))
			{
				bestDedicatedDevice = devices[i];
				bestDedicatedMesh = currentMesh;
				maxDedicatedMemory = currentMemorySize;
			}
			else if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
			         ((currentMesh && !bestIntegratedMesh) ||
			          (currentMesh == bestIntegratedMesh && currentMemorySize > maxIntegratedMemory)))
			{
				bestIntegratedDevice = devices[i];
				bestIntegratedMesh = currentMesh;
				maxIntegratedMemory = currentMemorySize;
			}
			else if (deviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
			         deviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
			         ((currentMesh && !bestFallbackMesh) ||
			          (currentMesh == bestFallbackMesh && currentMemorySize > maxFallbackMemory)))
			{
				bestFallbackDevice = devices[i];
				bestFallbackMesh = currentMesh;
				maxFallbackMemory = currentMemorySize;
			}
			(ctx->availableDevices)[i] = (char*)mi_malloc(strlen(deviceProperties.deviceName) +1);
			strcpy((ctx->availableDevices)[i], deviceProperties.deviceName);
			ANO_DEBUG_LOG(ano::Info, "Device %u is suitable: %s", i, ctx->availableDevices[i]);
		}
	}

	if (envDevice && !foundPreferredDevice)
	{
		ano::log(ano::Warn, "ANO_DEVICE=\"%s\" matched no suitable device; falling back to automatic selection.", envDevice);
	}

	if (foundPreferredDevice)
	{
		ctx->deviceCapabilities = populateCapabilities(ctx->physicalDevice);
		ctx->queueFamilyIndices = findQueueFamilies(ctx->physicalDevice, &(ctx->surface));
	}
	else
	{

		if (bestDedicatedDevice != VK_NULL_HANDLE)
		{
			ctx->physicalDevice = bestDedicatedDevice;
			ctx->deviceCapabilities = populateCapabilities(ctx->physicalDevice);
			ctx->queueFamilyIndices = findQueueFamilies(ctx->physicalDevice, &(ctx->surface));
		}
		else if (bestIntegratedDevice != VK_NULL_HANDLE)
		{
			ctx->physicalDevice = bestIntegratedDevice;
			ctx->deviceCapabilities = populateCapabilities(ctx->physicalDevice);
			ctx->queueFamilyIndices = findQueueFamilies(ctx->physicalDevice, &(ctx->surface));
		}
		else if (bestFallbackDevice != VK_NULL_HANDLE)
		{
			ano::log(ano::Warn, "No discrete or integrated GPU; using a fallback adapter (software or virtual).");
			ctx->physicalDevice = bestFallbackDevice;
			ctx->deviceCapabilities = populateCapabilities(ctx->physicalDevice);
			ctx->queueFamilyIndices = findQueueFamilies(ctx->physicalDevice, &(ctx->surface));
		}
		else
		{
			ano::log(ano::Fatal, "Failed to find a suitable GPU!");
			free(devices);
			return false;
		}
	}

	// Log the selected device.
	VkPhysicalDeviceProperties chosenProperties;
	vkGetPhysicalDeviceProperties(ctx->physicalDevice, &chosenProperties);
	ano::log(ano::Info, "Selected device: %s", chosenProperties.deviceName);

	ctx->msaaSamples = getMaxUsableSampleCount(ctx);
	ano::log(ano::Info, "MSAA samples used: %d", ctx->msaaSamples);

	//printf("Graphics family: %d\nCompute family: %d\nTransfer family: %d\nPresent family: %d\n", (ctx->queueFamilyIndices.graphicsFamily), (ctx->queueFamilyIndices.computeFamily), (ctx->queueFamilyIndices.transferFamily), (ctx->queueFamilyIndices.presentFamily));

	free(devices);
	return true;
}

VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices)
{
	// Query supported features via vkGetPhysicalDeviceFeatures2
	VkPhysicalDeviceMeshShaderFeaturesEXT queryMeshShaderFeatures = {};
	queryMeshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

	VkPhysicalDeviceDynamicRenderingFeaturesKHR queryDynamicRendering = {};
	queryDynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
	queryDynamicRendering.pNext = &queryMeshShaderFeatures;

	VkPhysicalDeviceVulkan11Features queryFeatures11 = {};
	queryFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	queryFeatures11.pNext = &queryDynamicRendering;

	VkPhysicalDeviceVulkan12Features queryFeatures12 = {};
	queryFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	queryFeatures12.pNext = &queryFeatures11;

	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &queryFeatures12;

	vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

	VkPhysicalDeviceFeatures deviceFeatures = {};
	ano_vk_apply_features<AnoVkCoreFeaturePolicy>(deviceFeatures, features2.features);

	// At most 4 unique queues
	VkDeviceQueueCreateInfo queueCreateInfos[4];
	uint32_t uniqueQueueFamilies[4] = {indices->graphicsFamily, indices->presentFamily, indices->computeFamily, indices->transferFamily};
	// Absent family is UINT32_MAX. Compute/transfer optional.
	bool familyPresent[4] = {indices->graphicsPresent, indices->presentPresent, indices->computePresent, indices->transferPresent};
	uint32_t queueCount = 0;
	// Shared queue priority, address valid until vkCreateDevice.
	const float queuePriority = 1.0f;

	for (uint32_t i = 0; i < 4; i++)
	{
		if (!familyPresent[i])
			continue;
		bool uniqueFamily = true;
		// Skip family indices already added
		for (uint32_t x = 0; x < i; x++)
		{
			if (familyPresent[x] && uniqueQueueFamilies[i] == uniqueQueueFamilies[x])
			{
				uniqueFamily = false;
				break;
			}
		}
		if (uniqueFamily)
		{
			VkDeviceQueueCreateInfo queueCreateInfo = {};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = uniqueQueueFamilies[i];
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos[queueCount] = queueCreateInfo;
			queueCount++;
		}
	}
	
	// Create the device
	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	// Request only what the device supports
	ano_vk_apply_features<AnoVk12FeaturePolicy>(features12, queryFeatures12);

	// Mesh off if unsupported or ANO_FORCE_NO_MESH_SHADER.
	bool meshSupported = queryMeshShaderFeatures.meshShader && !getenv("ANO_FORCE_NO_MESH_SHADER");

	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {};
	meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
	ano_vk_apply_features<AnoVkMeshFeaturePolicy>(meshShaderFeatures, queryMeshShaderFeatures);

	VkPhysicalDeviceVulkan11Features features11 = {};
	features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	ano_vk_apply_features<AnoVk11FeaturePolicy>(features11, queryFeatures11);

	VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeature = {};
	dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
	ano_vk_apply_features<AnoVkDynamicRenderingFeaturePolicy>(dynamicRenderingFeature, queryDynamicRendering);

	// Chain the mesh-shader feature struct only when enabling the extension.
	dynamicRenderingFeature.pNext = meshSupported ? (void*)&meshShaderFeatures : NULL;
	features11.pNext = &dynamicRenderingFeature; // 1.1 features -> dynamic rendering -> [mesh shader] -> NULL
	features12.pNext = &features11;

	VkDeviceCreateInfo createInfo = {};

	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &features12;
	createInfo.queueCreateInfoCount = queueCount;
	createInfo.pQueueCreateInfos = queueCreateInfos;
	createInfo.pEnabledFeatures = &deviceFeatures;

	// Required extensions + mesh shader when supported.
	const char* enabledExtensions[8];
	uint32_t enabledExtensionCount = 0;
	for (const AnoVkExtensionContract& contract : ANO_VK_DEVICE_EXTENSIONS)
	{
		if (!ano_vk_extension_active(contract.rule))
			continue;
		if (contract.rule == AnoVkExtensionRule::implied_by_feature && !meshSupported)
			continue;
		enabledExtensions[enabledExtensionCount++] = contract.name;
	}

	createInfo.enabledExtensionCount = enabledExtensionCount;
	createInfo.ppEnabledExtensionNames = enabledExtensions;
	ano::log(ano::Info, "Enabling %u device extensions (mesh shader: %s)", enabledExtensionCount, meshSupported ? "yes" : "no");

	if (vkCreateDevice(physicalDevice, &createInfo, NULL, device) != VK_SUCCESS)
	{
		ano::log(ano::Fatal, "Failed to create logical device!");
		return VK_ERROR_INITIALIZATION_FAILED;
	}
		
	// Queue handles
	vkGetDeviceQueue(*device, indices->graphicsFamily, 0, graphicsQueue);
	if (*graphicsQueue == NULL)
	{
		ano::log(ano::Fatal, "Failed to acquire graphics queue!");
		return VK_ERROR_INITIALIZATION_FAILED;	
	}
	vkGetDeviceQueue(*device, indices->presentFamily, 0, presentQueue);
	ANO_DEBUG_LOG(ano::Info, "PresentQueue: %p", (void*)presentQueue);
	if (*presentQueue == NULL)
	{
		ano::log(ano::Fatal, "Failed to acquire present queue!");
		return VK_ERROR_INITIALIZATION_FAILED;	
	}
	if (indices->computePresent)
	{
		vkGetDeviceQueue(*device, indices->computeFamily, 0, computeQueue);
		if (*computeQueue == NULL)
		{
			ano::log(ano::Fatal, "Failed to acquire compute queue!");
			return VK_ERROR_INITIALIZATION_FAILED;	
		}
	}
	if (indices->transferPresent)
	{
		vkGetDeviceQueue(*device, indices->transferFamily, 0, transferQueue);
		if (*transferQueue == NULL)
		{
			ano::log(ano::Fatal, "Failed to acquire transfer queue!");
			return VK_ERROR_INITIALIZATION_FAILED;
		}
	}
	else
	{
		// No TRANSFER_BIT reported: borrow graphics queue.
		*transferQueue = *graphicsQueue;
	}

	return VK_SUCCESS;
}
