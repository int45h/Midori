# Midori
A simple rendering framework, Vulkan and C++, WIP

## Dependencies ##
- SDL2 (+ SDL2_Image)
- Vulkan

## To Build ## 

### Meson + Ninja ###
```
meson setup build
cd build
ninja
```

### Muon + Samu ###
```
muon setup build
cd build
samu
```

## To build shaders ##
`./compile_shaders.sh`
(Append any shaders to the script if you add new ones)
