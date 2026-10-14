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
static VkShaderModule VK_CreateShader( const uint32_t *code, size_t size, const char *name )
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
	case VK_BLEND_MODULATE: // GL_ZERO, GL_SRC_COLOR
		state->blendEnable = VK_TRUE;
		state->srcColorBlendFactor = state->srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		state->dstColorBlendFactor = state->dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		break;
	case VK_BLEND_NONE:
	default:
		break;
	}
}

static qboolean VK_CreateImmPipelines( void )
{
	static const VkPrimitiveTopology topologies[VK_TOPOLOGY_COUNT] = {
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
	};
	VkShaderModule vert = VK_CreateShader( imm_vert_spv, sizeof( imm_vert_spv ), "imm.vert" );
	VkShaderModule frag = VK_CreateShader( imm_frag_spv, sizeof( imm_frag_spv ), "imm.frag" );
	qboolean ok = vert && frag;

	VkPipelineShaderStageCreateInfo stages[2] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
	};

	VkVertexInputBindingDescription binding = { 0, sizeof( vk_imm_vertex_t ), VK_VERTEX_INPUT_RATE_VERTEX };
	VkVertexInputAttributeDescription attribs[3] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof( vk_imm_vertex_t, position ) },
		{ 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( vk_imm_vertex_t, texcoord ) },
		{ 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof( vk_imm_vertex_t, color ) },
	};

	VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = 3,
		.pVertexAttributeDescriptions = attribs,
	};

	VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineRasterizationStateCreateInfo raster = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE, // 2D mode disables culling
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	// 2D mode: no depth test, no depth write
	VkPipelineDepthStencilStateCreateInfo depth = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
	};

	VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = ARRAYSIZE( dynamic_states ),
		.pDynamicStates = dynamic_states,
	};

	VkFormat color_format = VK_MainColorFormat();
	VkFormat depth_format = VK_MainDepthFormat();
	VkPipelineRenderingCreateInfo rendering = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &color_format,
		.depthAttachmentFormat = depth_format,
		.stencilAttachmentFormat = VK_FormatHasStencil( depth_format ) ? depth_format : VK_FORMAT_UNDEFINED,
	};

	for( int topology = 0; ok && topology < VK_TOPOLOGY_COUNT; topology++ )
	{
		for( int blend = 0; ok && blend < VK_BLEND_COUNT; blend++ )
		{
			VkPipelineColorBlendAttachmentState attachment;

			VK_SetBlend( &attachment, blend );

			VkPipelineInputAssemblyStateCreateInfo assembly = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
				.topology = topologies[topology],
			};

			VkPipelineColorBlendStateCreateInfo color_blend = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
				.attachmentCount = 1,
				.pAttachments = &attachment,
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

			if( vkCreateGraphicsPipelines( vk.device, vk_pipe.cache, 1, &info, NULL, &vk_pipe.imm[topology][blend] ) != VK_SUCCESS )
				ok = false;
			else
				VK_SetObjectName( VK_OBJECT_TYPE_PIPELINE, (uint64_t)vk_pipe.imm[topology][blend], "imm topology %d blend %d", topology, blend );
		}
	}

	if( vert )
		vkDestroyShaderModule( vk.device, vert, NULL );
	if( frag )
		vkDestroyShaderModule( vk.device, frag, NULL );

	return ok;
}

qboolean VK_InitPipelines( void )
{
	memset( &vk_pipe, 0, sizeof( vk_pipe ));

	if( !VK_CreateTextureSet( ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the bindless texture set\n" );
		return false;
	}

	VkPushConstantRange push = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.size = sizeof( vk_imm_push_t ),
	};

	VkPipelineLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vk_pipe.texture_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push,
	};

	if( vkCreatePipelineLayout( vk.device, &layout, NULL, &vk_pipe.layout ) != VK_SUCCESS )
		return false;

	VK_LoadPipelineCache();

	// every variant is built here: no pipeline is ever created during gameplay
	if( !VK_CreateImmPipelines( ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the immediate mode pipelines\n" );
		return false;
	}

	return true;
}

void VK_ShutdownPipelines( void )
{
	if( !vk.device )
		return;

	VK_SavePipelineCache();

	for( int topology = 0; topology < VK_TOPOLOGY_COUNT; topology++ )
	{
		for( int blend = 0; blend < VK_BLEND_COUNT; blend++ )
		{
			if( vk_pipe.imm[topology][blend] )
				vkDestroyPipeline( vk.device, vk_pipe.imm[topology][blend], NULL );
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

	memset( &vk_pipe, 0, sizeof( vk_pipe ));
}
