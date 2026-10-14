// the world data of the enhanced look: materials (vk_material.c) and the
// surfaces of the world model (vk_world.c VK_BuildSurfaceData)

struct material_s
{
	int albedo;
	int normal;      // tangent space, green up; 0 for none
	int rma;         // roughness, metalness, ambient occlusion
	int emissive;
	float emissive_strength;
	int flags;
	int pad0, pad1;
};

struct surface_s
{
	vec4 tangent;    // texture s axis
	vec4 bitangent;  // image up
	vec4 normal;     // facing the drawn side
	ivec4 info;      // x: light directions of its lightmap page, 0 for none
};

layout( std430, set = VK_SET_WORLD, binding = VK_BINDING_MATERIALS ) readonly buffer materials_s
{
	material_s materials[];
};

layout( std430, set = VK_SET_WORLD, binding = VK_BINDING_SURFACES ) readonly buffer surfaces_s
{
	surface_s surfaces[];
};
