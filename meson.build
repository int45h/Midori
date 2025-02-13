project('Midori', 'cpp', 
    version: '0.0.1', 
    license: 'MIT',
    meson_version: '>= 1.2.0',
    default_options: [
        'cpp_std=c++17',
        'buildtype=debug',
        'debug=true'
    ]
)

compiler = meson.get_compiler('cpp')

deps = []
deps += dependency('vulkan')

args = []
args += ['-lm', '-msse3']

src = [
    'src/simd_math/simd_math_sse.cc', 
    'src/platform/file/file_posix.cc', 
    'src/platform/shared_library/library_posix.cc',
    'src/vma/vma_usage.cc', 
    'src/renderer/renderer_vk/renderer_vk_helpers.cc', 
    'src/renderer/renderer_vk/renderer_vk.cc', 
    'include/vk_bootstrap/VkBootstrap.cpp', 
    'src/stb_image/stb_image_usage.cc',
    'src/main.cc']

if get_option('use_sdl') == false
    deps += compiler.find_library('xcb', required: true)
    src += 'src/platform/window/window_xcb_vulkan.cc'    
else
    deps += compiler.find_library('SDL2', required: true)
    deps += compiler.find_library('SDL2_image', required: true)
    src += 'src/platform/window/window_sdl_vulkan.cc'
endif


include = include_directories(
    'include', 
    'include/platform',
    'include/renderer',
    'include/simd_math',
    'include/stb_image',
    'include/tinygltf',
    'include/tinyobj',
    'include/vk_bootstrap',
    'include/vma'
)

executable(
    'midori', 
    src,
    cpp_args: args,
    dependencies: deps,
    include_directories: include
)