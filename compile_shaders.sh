#!/bin/bash

#glslang -V shaders/test_shadow.vsh -o shaders/test_vert_shadow.h --vn test_vsh_shadow_spirv -S vert
#glslang -V shaders/test_shadow.fsh -o shaders/test_frag_shadow.h --vn test_fsh_shadow_spirv -S frag
#glslang -V shaders/test.vsh -o shaders/test_vert.h --vn test_vsh_spirv -S vert
#glslang -V shaders/test.fsh -o shaders/test_frag.h --vn test_fsh_spirv -S frag
#glslang -V shaders/test2.vsh -o shaders/test_vert_2.h --vn test_vsh_2_spirv -S vert
#glslang -V shaders/test2.fsh -o shaders/test_frag_2.h --vn test_fsh_2_spirv -S frag

glslang -V shaders/test_shadow.vsh -o shaders/spv/test_shadow_vert.spv -S vert
glslang -V shaders/test_shadow.fsh -o shaders/spv/test_shadow_frag.spv -S frag
glslang -V shaders/test.vsh -o shaders/spv/test_vert.spv -S vert
glslang -V shaders/test.fsh -o shaders/spv/test_frag.spv -S frag
glslang -V shaders/test2.vsh -o shaders/spv/test_vert_2.spv -S vert
glslang -V shaders/test2.fsh -o shaders/spv/test_frag_2.spv -S frag
glslang -V shaders/empty.vsh -o shaders/spv/empty_vsh.spv -S vert
glslang -V shaders/empty.fsh -o shaders/spv/empty_fsh.spv -S frag