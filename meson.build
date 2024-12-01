project('Midori', 'cpp', 
    version: '0.0.1', 
    license: 'MIT',
    meson_version: '>= 1.2.0',
    default_options: [
        'cpp_std=c++17',
        'buildtype=debugoptimized',
        'debug=true'
    ]
)

compiler = meson.get_compiler('cpp')

deps = []
deps += dependency('vulkan')
deps += compiler.find_library('SDL2', required: true)
deps += compiler.find_library('SDL2_image', required: true)

args = []
args += ['-lm', '-msse3']

src = [
    'src/simd_math/simd_math_sse.cc', 
    'src/file/file.cc', 
    'src/vma/vma_usage.cc', 
    'src/renderer/renderer_vk/renderer_vk_helpers.cc', 
    'include/vk_bootstrap/VkBootstrap.cpp', 
    'src/main.cc']
executable(
    'midori', 
    src,
    cpp_args: args,
    dependencies: deps
)