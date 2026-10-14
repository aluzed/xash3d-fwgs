/*
vk_pipeline.c - descriptors, shaders and pipeline objects
Copyright (C) 2026 aluzed

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "vk_local.h"

// SPIR-V compiled by glslc at build time (see wscript)
static const uint32_t imm_vert_spv[] =
#include "shaders/imm.vert.inc"
;

static const uint32_t imm_frag_spv[] =
#include "shaders/imm.frag.inc"
;

static const uint32_t world_vert_spv[] =
#include "shaders/world.vert.inc"
;

static const uint32_t world_frag_spv[] =
#include "shaders/world.frag.inc"
;

#define VK_PIPELINE_CACHE_FILE "vulkan_pipelines.bin"

vk_pipelines_t vk_pipe;

/*
=================
Bindless texture set
=================
*/
static qboolean VK_CreateTextureSet( void )
{
	VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
		| VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
		| VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = 1,
		.pBindingFlags = &flags,
	};

	VkDescriptorSetLayoutBinding binding = {
		.binding = VK_BINDING_TEXTURES,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = VK_MAX_TEXTURES,
		.stageFlags = VK_SHADER_STAGE_ALL,
	};

	VkDescriptorSetLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &binding_flags,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if( vkCreateDescriptorSetLayout( vk.device, &layout, NULL, &vk_pipe.texture_layout ) != VK_SUCCESS )
		return false;

	VkDescriptorPoolSize size = {
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = VK_MAX_TEXTURES,
	};

	VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &size,
	};

	if( vkCreateDescriptorPool( vk.device, &pool, NULL, &vk_pipe.texture_pool ) != VK_SUCCESS )
		return false;

	VkDescriptorSetAllocateInfo alloc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_pipe.texture_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vk_pipe.texture_layout,
	};

	if( vkAllocateDescriptorSets( vk.device, &alloc, &vk_pipe.texture_set ) != VK_SUCCESS )
		return false;

	VK_SetObjectName( VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)vk_pipe.texture_layout, "textures layout" );
	VK_SetObjectName( VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)vk_pipe.texture_set, "textures" );

	return true;
}

/*
=================
Fog table set: one per frame in flight, written by vk_fog.c
=================
*/
static qboolean VK_CreateFogSets( void )
{
	VkDescriptorSetLayoutBinding binding = {
		.binding = VK_BINDING_FOG,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	VkDescriptorSetLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if( vkCreateDescriptorSetLayout( vk.device, &layout, NULL, &vk_pipe.fog_layout ) != VK_SUCCESS )
		return false;

	VkDescriptorPoolSize size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = VK_FRAMES_IN_FLIGHT,
	};

	VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = VK_FRAMES_IN_FLIGHT,
		.poolSizeCount = 1,
		.pPoolSizes = &size,
	};

	if( vkCreateDescriptorPool( vk.device, &pool, NULL, &vk_pipe.fog_pool ) != VK_SUCCESS )
		return false;

	VkDescriptorSetLayout layouts[VK_FRAMES_IN_FLIGHT];
	for( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ )
		layouts[i] = vk_pipe.fog_layout;

	VkDescriptorSetAllocateInfo alloc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_pipe.fog_pool,
		.descriptorSetCount = VK_FRAMES_IN_FLIGHT,
		.pSetLayouts = layouts,
	};

	if( vkAllocateDescriptorSets( vk.device, &alloc, vk_pipe.fog_sets ) != VK_SUCCESS )
		return false;

	VK_SetObjectName( VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)vk_pipe.fog_layout, "fog layout" );
	return true;
}

void VK_WriteFogDescriptor( int frame, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range )
{
	VkDescriptorBufferInfo info = {
		.buffer = buffer,
		.offset = offset,
		.range = range,
	};

	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = vk_pipe.fog_sets[frame],
		.dstBinding = VK_BINDING_FOG,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &info,
	};

	vkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

/*
=================
World data sets: the material and surface buffers, one set per frame in
flight, rewritten at the start of a frame when a buffer changed
=================
*/
static qboolean VK_CreateWorldDataSets( void )
{
	VkDescriptorBindingFlags flags[2] = { VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT };

	VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = 2,
		.pBindingFlags = flags,
	};

	VkDescriptorSetLayoutBinding bindings[2] = {
		{ VK_BINDING_MATERIALS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
		{ VK_BINDING_SURFACES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
	};

	VkDescriptorSetLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &binding_flags,
		.bindingCount = 2,
		.pBindings = bindings,
	};

	if( vkCreateDescriptorSetLayout( vk.device, &layout, NULL, &vk_pipe.world_layout ) != VK_SUCCESS )
		return false;

	VkDescriptorPoolSize size = {
		.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 2 * VK_FRAMES_IN_FLIGHT,
	};

	VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = VK_FRAMES_IN_FLIGHT,
		.poolSizeCount = 1,
		.pPoolSizes = &size,
	};

	if( vkCreateDescriptorPool( vk.device, &pool, NULL, &vk_pipe.world_pool ) != VK_SUCCESS )
		return false;

	VkDescriptorSetLayout layouts[VK_FRAMES_IN_FLIGHT];
	for( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ )
		layouts[i] = vk_pipe.world_layout;

	VkDescriptorSetAllocateInfo alloc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_pipe.world_pool,
		.descriptorSetCount = VK_FRAMES_IN_FLIGHT,
		.pSetLayouts = layouts,
	};

	if( vkAllocateDescriptorSets( vk.device, &alloc, vk_pipe.world_sets ) != VK_SUCCESS )
		return false;

	VK_SetObjectName( VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)vk_pipe.world_layout, "world data layout" );
	return true;
}

// takes effect from the next frame that starts
void VK_WriteWorldDataDescriptor( int binding, VkBuffer buffer, VkDeviceSize range )
{
	vk_pipe.world_buffers[binding] = buffer;
	vk_pipe.world_ranges[binding] = range;
	vk_pipe.world_generation++;
}

void VK_WorldDataBeginFrame( int frame )
{
	VkDescriptorBufferInfo infos[2];
	VkWriteDescriptorSet writes[2];
	int count = 0;

	if( vk_pipe.world_written[frame] == vk_pipe.world_generation )
		return;

	for( int i = 0; i < 2; i++ )
	{
		if( !vk_pipe.world_buffers[i] )
			continue;

		infos[count] = (VkDescriptorBufferInfo){ vk_pipe.world_buffers[i], 0, vk_pipe.world_ranges[i] };
		writes[count] = (VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = vk_pipe.world_sets[frame],
			.dstBinding = i,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &infos[count],
		};
		count++;
	}

	if( count )
		vkUpdateDescriptorSets( vk.device, count, writes, 0, NULL );

	vk_pipe.world_written[frame] = vk_pipe.world_generation;
}

/*
=================
VK_BindDescriptorSets

the textures, the fog table and the world data of the frame being
recorded: the same for every draw of a command buffer, so bound once
(every graphics pipeline shares the layout; compute passes bind their own
sets on the other bind point)
=================
*/
void VK_BindDescriptorSets( VkCommandBuffer cmd )
{
	if( cmd == vk_pipe.bound_cmd )
		return;

	VkDescriptorSet sets[3] = { vk_pipe.texture_set, vk_pipe.fog_sets[VK_FrameIndex( )], vk_pipe.world_sets[VK_FrameIndex( )] };

	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe.layout, 0, 3, sets, 0, NULL );
	vk_pipe.bound_cmd = cmd;
}

// a command buffer begins: nothing is bound in it
void VK_DescriptorSetsReset( void )
{
	vk_pipe.bound_cmd = VK_NULL_HANDLE;
}

void VK_WriteTextureDescriptor( int texnum, VkImageView view, VkSampler sampler )
{
	VkDescriptorImageInfo image = {
		.sampler = sampler,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = vk_pipe.texture_set,
		.dstBinding = VK_BINDING_TEXTURES,
		.dstArrayElement = texnum,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &image,
	};

	vkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

/*
=================
Pipeline cache, persisted in the writable game directory
=================
*/
static void VK_LoadPipelineCache( void )
{
	fs_offset_t size = 0;
	byte *data = gEngfuncs.fsapi->LoadFile( VK_PIPELINE_CACHE_FILE, &size, true );

	// only feed data written by this exact device and driver
	if( data && size > (fs_offset_t)( 16 + VK_UUID_SIZE ))
	{
		uint32_t vendor, device;

		memcpy( &vendor, data + 8, 4 );
		memcpy( &device, data + 12, 4 );

		if( vendor != vk.props.vendorID || device != vk.props.deviceID
			|| memcmp( data + 16, vk.props.pipelineCacheUUID, VK_UUID_SIZE ))
			size = 0;
	}
	else size = 0;

	VkPipelineCacheCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
		.initialDataSize = size,
		.pInitialData = size ? data : NULL,
	};

	if( vkCreatePipelineCache( vk.device, &info, NULL, &vk_pipe.cache ) != VK_SUCCESS )
		vk_pipe.cache = VK_NULL_HANDLE;

	if( data )
		Mem_Free( data );
}

static void VK_SavePipelineCache( void )
{
	size_t size = 0;
	void *data;

	if( !vk_pipe.cache || vkGetPipelineCacheData( vk.device, vk_pipe.cache, &size, NULL ) != VK_SUCCESS || !size )
		return;

	data = Mem_Malloc( r_temppool, size );

	if( vkGetPipelineCacheData( vk.device, vk_pipe.cache, &size, data ) == VK_SUCCESS )
		gEngfuncs.fsapi->WriteFile( VK_PIPELINE_CACHE_FILE, data, size );

	Mem_Free( data );
}

/*
=================
Immediate mode pipelines (2D, TriAPI)
=================
*/
VkShaderModule VK_CreateShader( const uint32_t *code, size_t size, const char *name )
{
	VkShaderModule module;

	VkShaderModuleCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = code,
	};

	if( vkCreateShaderModule( vk.device, &info, NULL, &module ) != VK_SUCCESS )
		return VK_NULL_HANDLE;

	VK_SetObjectName( VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)module, "%s", name );
	return module;
}

static void VK_SetBlend( VkPipelineColorBlendAttachmentState *state, vk_blend_t blend )
{
	memset( state, 0, sizeof( *state ));
	state->colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	state->colorBlendOp = state->alphaBlendOp = VK_BLEND_OP_ADD;

	switch( blend )
	{
	case VK_BLEND_ALPHA: // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
		state->blendEnable = VK_TRUE;
		state->srcColorBlendFactor = state->srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		state->dstColorBlendFactor = state->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		break;
	case VK_BLEND_ADD: // GL_SRC_ALPHA, GL_ONE
		state->blendEnable = VK_TRUE;
		state->srcColorBlendFactor = state->srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		state->dstColorBlendFactor = state->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		break;
	case VK_BLEND_OVERBRIGHT: // GL_DST_COLOR, GL_SRC_COLOR: twice the product
		state->blendEnable = VK_TRUE;
		state->srcColorBlendFactor = state->srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		state->dstColorBlendFactor = state->dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		break;
	case VK_BLEND_ONE_ONE: // GL_ONE, GL_ONE
		state->blendEnable = VK_TRUE;
		state->srcColorBlendFactor = state->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		state->dstColorBlendFactor = state->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		break;
	case VK_BLEND_DSTCOLOR_INVSRCALPHA: // GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA
		state->blendEnable = VK_TRUE;
		state->srcColorBlendFactor = state->srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		state->dstColorBlendFactor = state->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		break;
	case VK_BLEND_PREMULTIPLIED: // GL_ONE, GL_ONE_MINUS_SRC_ALPHA
		state->blendEnable = VK_TRUE;
		state->srcColorBlendFactor = state->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		state->dstColorBlendFactor = state->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		break;
	case VK_BLEND_MODULATE: // GL_ZERO, GL_SRC_COLOR
		state->blendEnable = VK_TRUE;
		state->srcColorBlendFactor = state->srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		state->dstColorBlendFactor = state->dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		break;
	case VK_BLEND_NO_COLOR: // glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE )
		state->colorWriteMask = 0;
		break;
	case VK_BLEND_NONE:
	default:
		break;
	}
}

typedef struct vk_pipeline_desc_s
{
	const char *name;
	VkShaderModule vert, frag;
	const VkVertexInputBindingDescription *binding;
	const VkVertexInputAttributeDescription *attribs;
	int num_attribs;
	VkPrimitiveTopology topology;
	vk_blend_t blend;
	qboolean depth_test;
	qboolean depth_write;
	qboolean depth_equal; // GL_EQUAL instead of GL_LEQUAL
	qboolean depth_bias;  // glPolygonOffset, set with vkCmdSetDepthBias
	qboolean dynamic_gl;  // cull mode and depth test, write and function set per draw
	VkCullModeFlags cull;
	vk_target_t target;   // colour target format, and how the fragment shader writes to it
} vk_pipeline_desc_t;

/*
=================
VK_TargetSpecialization

the fragment shaders' constants (output_common.glsl): an HDR target gets
linear colours. GL blends in gamma space, a gamma 2.2 power is a product
of powers, so GL_DST_COLOR, GL_SRC_COLOR blending (twice the product)
needs its source scaled by 2^1.2 to stay what GL gives, unclamped.
=================
*/
typedef struct
{
	VkBool32 linear_output;
	float output_scale;
} vk_output_constants_t;

static const VkSpecializationMapEntry vk_output_entries[2] =
{
	{ 0, offsetof( vk_output_constants_t, linear_output ), sizeof( VkBool32 ) },
	{ 1, offsetof( vk_output_constants_t, output_scale ), sizeof( float ) },
};

static VkFormat VK_TargetFormat( vk_target_t target )
{
	return target == VK_TARGET_HDR ? VK_HDR_COLOR_FORMAT : VK_MainColorFormat();
}

static VkPipeline VK_CreateGraphicsPipeline( const vk_pipeline_desc_t *desc )
{
	VkPipelineColorBlendAttachmentState attachment;
	VkPipeline pipeline;

	vk_output_constants_t constants = {
		.linear_output = desc->target == VK_TARGET_HDR,
		.output_scale = desc->target == VK_TARGET_HDR && desc->blend == VK_BLEND_OVERBRIGHT ? 2.29739671f : 1.0f,
	};

	VkSpecializationInfo specialization = {
		.mapEntryCount = ARRAYSIZE( vk_output_entries ),
		.pMapEntries = vk_output_entries,
		.dataSize = sizeof( constants ),
		.pData = &constants,
	};

	// the LDR pipelines keep the shaders' defaults
	VkPipelineShaderStageCreateInfo stages[2] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = desc->vert, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = desc->frag, .pName = "main",
			.pSpecializationInfo = desc->target == VK_TARGET_HDR ? &specialization : NULL },
	};

	VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = desc->binding,
		.vertexAttributeDescriptionCount = desc->num_attribs,
		.pVertexAttributeDescriptions = desc->attribs,
	};

	VkPipelineInputAssemblyStateCreateInfo assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = desc->topology,
	};

	VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	// the viewport is flipped, so GL's counter-clockwise front faces stay so
	VkPipelineRasterizationStateCreateInfo raster = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = desc->cull,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = desc->depth_bias,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineDepthStencilStateCreateInfo depth = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = desc->depth_test,
		.depthWriteEnable = desc->depth_write,
		.depthCompareOp = desc->depth_equal ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL,
	};

	VK_SetBlend( &attachment, desc->blend );

	VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment,
	};

	VkDynamicState dynamic_states[12] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
	};

	if( desc->depth_bias )
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;

	if( desc->dynamic_gl )
	{
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_CULL_MODE;
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_COMPARE_OP;
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE;
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_OP;
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
		dynamic_states[dynamic.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
	}

	VkFormat color_format = VK_TargetFormat( desc->target );
	VkFormat depth_format = VK_MainDepthFormat();
	VkPipelineRenderingCreateInfo rendering = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &color_format,
		.depthAttachmentFormat = depth_format,
		.stencilAttachmentFormat = VK_FormatHasStencil( depth_format ) ? depth_format : VK_FORMAT_UNDEFINED,
	};

	VkGraphicsPipelineCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &rendering,
		.stageCount = 2,
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &assembly,
		.pViewportState = &viewport,
		.pRasterizationState = &raster,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth,
		.pColorBlendState = &color_blend,
		.pDynamicState = &dynamic,
		.layout = vk_pipe.layout,
	};

	if( vkCreateGraphicsPipelines( vk.device, vk_pipe.cache, 1, &info, NULL, &pipeline ) != VK_SUCCESS )
		return VK_NULL_HANDLE;

	VK_SetObjectName( VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, "%s", desc->name );
	return pipeline;
}

static qboolean VK_CreateImmPipelines( vk_target_t target )
{
	static const VkPrimitiveTopology topologies[VK_TOPOLOGY_COUNT] = {
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
	};
	static const VkVertexInputBindingDescription binding = { 0, sizeof( vk_imm_vertex_t ), VK_VERTEX_INPUT_RATE_VERTEX };
	static const VkVertexInputAttributeDescription attribs[3] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof( vk_imm_vertex_t, position ) },
		{ 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( vk_imm_vertex_t, texcoord ) },
		{ 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof( vk_imm_vertex_t, color ) },
	};
	VkShaderModule vert = VK_CreateShader( imm_vert_spv, sizeof( imm_vert_spv ), "imm.vert" );
	VkShaderModule frag = VK_CreateShader( imm_frag_spv, sizeof( imm_frag_spv ), "imm.frag" );
	qboolean ok = vert && frag;

	for( int topology = 0; ok && topology < VK_TOPOLOGY_COUNT; topology++ )
	{
		for( int blend = 0; ok && blend < VK_BLEND_ALL; blend++ )
		{
			char name[64];

			// culling, depth test and polygon offset follow the GL state of each draw
			vk_pipeline_desc_t desc = {
				.name = name,
				.vert = vert, .frag = frag,
				.binding = &binding, .attribs = attribs, .num_attribs = 3,
				.topology = topologies[topology],
				.blend = blend,
				.depth_bias = true,
				.dynamic_gl = true,
				.cull = VK_CULL_MODE_NONE,
				.target = target,
			};

			Q_snprintf( name, sizeof( name ), "imm target %d topology %d blend %d", target, topology, blend );
			vk_pipe.imm[target][topology][blend] = VK_CreateGraphicsPipeline( &desc );
			ok = vk_pipe.imm[target][topology][blend] != VK_NULL_HANDLE;
		}
	}

	if( vert )
		vkDestroyShaderModule( vk.device, vert, NULL );
	if( frag )
		vkDestroyShaderModule( vk.device, frag, NULL );

	return ok;
}

static qboolean VK_CreateWorldPipelines( vk_target_t target )
{
	// glpoly2_t vertices: position, texture st, lightmap st
	static const VkVertexInputBindingDescription binding = { 0, VERTEXSIZE * sizeof( float ), VK_VERTEX_INPUT_RATE_VERTEX };
	static const VkVertexInputAttributeDescription attribs[3] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
		{ 1, 0, VK_FORMAT_R32G32_SFLOAT, 3 * sizeof( float ) },
		{ 2, 0, VK_FORMAT_R32G32_SFLOAT, 5 * sizeof( float ) },
	};
	VkShaderModule vert = VK_CreateShader( world_vert_spv, sizeof( world_vert_spv ), "world.vert" );
	VkShaderModule frag = VK_CreateShader( world_frag_spv, sizeof( world_frag_spv ), "world.frag" );
	qboolean ok = vert && frag;

	// every combination brush surfaces use, GL_Cull( GL_FRONT ) for all
	for( int blend = 0; ok && blend < VK_BLEND_ALL; blend++ )
	{
		for( int write = 0; ok && write < 2; write++ )
		{
			for( int equal = 0; ok && equal < 2; equal++ )
			{
				char name[64];

				vk_pipeline_desc_t desc = {
					.name = name,
					.vert = vert, .frag = frag,
					.binding = &binding, .attribs = attribs, .num_attribs = 3,
					.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
					.blend = blend,
					.depth_test = true,
					.depth_write = write,
					.depth_equal = equal,
					.depth_bias = true,
					.cull = VK_CULL_MODE_FRONT_BIT,
					.target = target,
				};

				Q_snprintf( name, sizeof( name ), "world target %d blend %d write %d equal %d", target, blend, write, equal );
				vk_pipe.world[target][blend][write][equal] = VK_CreateGraphicsPipeline( &desc );
				ok = vk_pipe.world[target][blend][write][equal] != VK_NULL_HANDLE;
			}
		}
	}

	if( vert )
		vkDestroyShaderModule( vk.device, vert, NULL );
	if( frag )
		vkDestroyShaderModule( vk.device, frag, NULL );

	return ok;
}

// for the colour target being drawn into
VkPipeline VK_WorldPipeline( vk_blend_t blend, qboolean depth_write, qboolean depth_equal )
{
	return vk_pipe.world[VK_ColorTarget()][blend][depth_write ? 1 : 0][depth_equal ? 1 : 0];
}

VkPipeline VK_ImmPipeline( vk_topology_t topology, vk_blend_t blend )
{
	return vk_pipe.imm[VK_ColorTarget()][topology][blend];
}

/*
=================
VK_CreateHDRPipelines

the classic look never needs them: built the first time the HDR scene is
turned on, outside any frame
=================
*/
qboolean VK_CreateHDRPipelines( void )
{
	if( vk_pipe.hdr_ready )
		return true;

	vk_pipe.hdr_ready = VK_CreateImmPipelines( VK_TARGET_HDR ) && VK_CreateWorldPipelines( VK_TARGET_HDR );

	if( !vk_pipe.hdr_ready )
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the HDR pipelines\n" );

	return vk_pipe.hdr_ready;
}

qboolean VK_InitPipelines( void )
{
	memset( &vk_pipe, 0, sizeof( vk_pipe ));

	if( !VK_CreateTextureSet( ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the bindless texture set\n" );
		return false;
	}

	if( !VK_CreateFogSets( ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the fog sets\n" );
		return false;
	}

	if( !VK_CreateWorldDataSets( ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the world data sets\n" );
		return false;
	}

	VkDescriptorSetLayout set_layouts[3] = { vk_pipe.texture_layout, vk_pipe.fog_layout, vk_pipe.world_layout };

	VkPushConstantRange push = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.size = VK_PUSH_CONSTANTS_SIZE, // every pass shares the layout
	};

	VkPipelineLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 3,
		.pSetLayouts = set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push,
	};

	if( vkCreatePipelineLayout( vk.device, &layout, NULL, &vk_pipe.layout ) != VK_SUCCESS )
		return false;

	VK_LoadPipelineCache();

	// every variant is built here: no pipeline is ever created during gameplay
	if( !VK_CreateImmPipelines( VK_TARGET_LDR ) || !VK_CreateWorldPipelines( VK_TARGET_LDR ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the pipelines\n" );
		return false;
	}

	return true;
}

void VK_ShutdownPipelines( void )
{
	if( !vk.device )
		return;

	VK_SavePipelineCache();

	for( int target = 0; target < VK_TARGET_COUNT; target++ )
	{
		for( int topology = 0; topology < VK_TOPOLOGY_COUNT; topology++ )
		{
			for( int blend = 0; blend < VK_BLEND_ALL; blend++ )
			{
				if( vk_pipe.imm[target][topology][blend] )
					vkDestroyPipeline( vk.device, vk_pipe.imm[target][topology][blend], NULL );
			}
		}

		for( int i = 0; i < VK_BLEND_ALL; i++ )
		{
			for( int j = 0; j < 4; j++ )
			{
				if( vk_pipe.world[target][i][j >> 1][j & 1] )
					vkDestroyPipeline( vk.device, vk_pipe.world[target][i][j >> 1][j & 1], NULL );
			}
		}
	}
	if( vk_pipe.cache )
		vkDestroyPipelineCache( vk.device, vk_pipe.cache, NULL );
	if( vk_pipe.layout )
		vkDestroyPipelineLayout( vk.device, vk_pipe.layout, NULL );
	if( vk_pipe.texture_pool )
		vkDestroyDescriptorPool( vk.device, vk_pipe.texture_pool, NULL );
	if( vk_pipe.texture_layout )
		vkDestroyDescriptorSetLayout( vk.device, vk_pipe.texture_layout, NULL );
	if( vk_pipe.fog_pool )
		vkDestroyDescriptorPool( vk.device, vk_pipe.fog_pool, NULL );
	if( vk_pipe.fog_layout )
		vkDestroyDescriptorSetLayout( vk.device, vk_pipe.fog_layout, NULL );
	if( vk_pipe.world_pool )
		vkDestroyDescriptorPool( vk.device, vk_pipe.world_pool, NULL );
	if( vk_pipe.world_layout )
		vkDestroyDescriptorSetLayout( vk.device, vk_pipe.world_layout, NULL );

	memset( &vk_pipe, 0, sizeof( vk_pipe ));
}
