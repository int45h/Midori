#!/bin/bash

glslang -V shaders/test.vsh -o shaders/test_vert.h --vn test_vsh_spirv -S vert
glslang -V shaders/test.fsh -o shaders/test_frag.h --vn test_fsh_spirv -S frag