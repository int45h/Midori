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
deps += compiler.find_library('SDL2', required: true) #dependency('SDL2')
deps += compiler.find_library('SDL2_image', required: true) #dependency('SDL2_image')

args = []
args += ['-lm', '-msse3']

src = ['src/main.cc', 'src/vk_bootstrap/VkBootstrap.cpp']
executable(
    'midori', 
    src,
    cpp_args: args,
    dependencies: deps
)

