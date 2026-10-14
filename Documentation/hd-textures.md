### HD (external) textures support

Xash3D supports loading texture replacements in TGA format for almost all types of models in the game, except alias models at this time.

Textures are expected to be located at:
* `modfolder/materials/<mapname>` - for a specific map
* `modfolder/materials/common` - common for all maps
* `modfolder/materials/decals` - for decals
* `modfolder/materials/models/<model>` - for models (texture name must match the internal texture name in the model)

Support for high-resolution textures is enabled setting `host_allow_materials` cvar to `1` or in the menu, in "Video options" section.

#### Xash3D FWGS additions

In addition to paths above, Xash3D FWGS checks following paths:

* `modfolder/materials/sprites/<sprite>` - for sprites, except HUD sprites

Also, to check which texture replacements are loaded successfully, failed or weren't found, a mod developer can set `host_allow_materials` cvar value to `2`. The engine will spew log at any developer level in the following format:

```
Looking for <replacement> replacement... <status code> (<path relative to mod directory>)
```

Status codes:
* `OK` - texture replacement file was found and loaded into GPU memory successfully
* `FAIL` - texture file was found but hasn't been parsed or loaded successfully. Refer to engine log for more details.
* `MISS` - texture file wasn't found

Example:
```
Looking for maps/bounce.bsp:!waterblue tex replacement...OK (materials/common/!waterblue.tga)
Looking for maps/bounce.bsp:!waterblue_luma tex replacement...MISS (not found)
Looking for {shot2 decal replacement...MISS (materials/decals/{shot2.tga)
Looking for {shot4 decal replacement...MISS (materials/decals/{shot4.tga)
Looking for {shot3 decal replacement...MISS (materials/decals/{shot3.tga)
Looking for models/gman tex replacement...FAIL (materials/models/gman/GMan_Case1.tga)
Looking for models/gman tex replacement...FAIL (materials/models/gman/inside_1.tga)
```


#### Materials (Vulkan renderer)

With the Vulkan renderer (`ref_vk`), a texture can also have a material:
a `.mat` file at the same place as the TGA replacement, with the same name
(`materials/maps/<map>.bsp/<texture>.mat`, then `materials/common/<texture>.mat`
for world textures; the `maps/<map>.bsp` directory is the map model's name,
as the engine uses it for the TGA files). It is a list of `key value`
pairs, `//` comments allowed:

```
// materials/common/SandCrtLrgSd.mat
albedo   SandCrtLrgSd.ktx2       // replaces the texture, every look
normal   SandCrtLrgSd_n.ktx2     // tangent space, green up (OpenGL)
rma      SandCrtLrgSd_rma.ktx2   // roughness, metalness, ambient occlusion
emissive SandCrtLrgSd_e.ktx2     // emitted colour
emissive_strength 2
reflective 1                     // for the ray traced reflections
```

A file name without a directory is next to the `.mat`. Any format the
engine reads works: TGA, PNG, DDS, and KTX2 holding BC4, BC5 (normal maps),
BC6H or BC7 blocks with their mip levels. The maps are sampled with the
original texture coordinates, so they can have any size.

The albedo replaces the game texture in both looks. The other maps are
only used with `r_vk_quality enhanced`, on world surfaces for now: the
normal map and the occlusion change the lightmap, the material adds the
specular reflection of the main light and the emission. GoldSrc lightmaps
don't say where the light comes from, so `ref_vk` works it out at map load
from the map's light entities, its sun and its texture lights (textures
with a `~` in their name), when a world material has a normal map.

`host_allow_materials 2` also logs the materials found:

```
Looking for maps/de_dust2.bsp:sandcrtlrgsd material...OK
```
