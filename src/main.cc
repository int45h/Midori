#include <cstddef>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>
#define PI 3.14159265359

#include <immintrin.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_vulkan.h>
#include <vector>
#include <vulkan/vulkan_core.h>

#include <time.h>

#define VMA_IMPLEMENTATION
#include "vk_bootstrap/VkBootstrap.h"
#include "vma/vk_mem_alloc.h"

//#define TINYGLTF_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_WRITE_IMPLEMENTATION
//#include "tinygltf/tiny_gltf.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tinyobj/tiny_obj_loader.h"

// Shaders
#include "../shaders/test_vert.h"
#include "../shaders/test_frag.h"
#include "../shaders/test_vert_2.h"
#include "../shaders/test_frag_2.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;
typedef size_t usize;

#define LOG_ERROR(err, ...) fprintf(stderr, "error: " err, ##__VA_ARGS__)
#define MIN_VAL(a,b) (((a)<(b))?(a):(b))
#define MAX_VAL(a,b) (((a)>(b))?(a):(b))

#define VERTEX_SIZE 8

typedef enum
{
    MD_SUCCESS,
    MD_ERROR_UNKNOWN,
    MD_ERROR_FILE_NOT_FOUND,
    MD_ERROR_FILE_READ_FAILURE,
    MD_ERROR_MEMORY_ALLOCATION_FAILURE,
    MD_ERROR_OBJ_LOADING_FAILURE,
    MD_ERROR_WINDOW_FAILURE,
    MD_ERROR_VULKAN_INSTANCE_FAILURE,
    MD_ERROR_VULKAN_PHYSICAL_DEVICE_FAILURE,
    MD_ERROR_VULKAN_LOGICAL_DEVICE_FAILURE,
    MD_ERROR_VULKAN_QUEUE_NOT_PRESENT,
    MD_ERROR_VULKAN_SWAPCHAIN_FAILURE,
    MD_ERROR_VULKAN_SWAPCHAIN_IMAGE_FAILURE,
    MD_ERROR_VULKAN_SWAPCHAIN_IMAGE_VIEW_FAILURE
}
MdResult;

#define MD_CHECK(result, err, ...) if (result != MD_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return result;}
#define MD_CHECK_VOID(result, err, ...) if (result != MD_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return;}
#define MD_CHECK_ANY(result, ret, err, ...) if (result != MD_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return ret;}

#define VK_CHECK(result, err, ...) if (result != VK_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return result;}
#define VK_CHECK_VOID(result, err, ...) if (result != VK_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return;}
#define VK_CHECK_ANY(result, ret, err, ...) if (result != VK_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return ret;}

#pragma region [ SIMD Vector Math ]
f32 hadd_sse3(const __m128 &v)
{
    __m128 shuf = _mm_movehdup_ps(v);
    __m128 sums = _mm_add_ps(v, shuf);
    shuf = _mm_movehl_ps(sums, sums);
    sums = _mm_add_ps(shuf, sums);
    return _mm_cvtss_f32(sums);
}

enum Vector4_Component{ X=0, Y=1, Z=2, W=3 };

struct Vector4
{
    union
    {
        float xyzw[4];
        __m128 xyzw_m128;
    };
    Vector4(f32 x, f32 y, f32 z, f32 w) : xyzw_m128(_mm_set_ps(w,z,y,x)) {  }
    Vector4(f32 v[4]) : xyzw_m128(_mm_load_ps(v)) {  }
    Vector4(const __m128 &v) : xyzw_m128(v) {}
    Vector4(const Vector4 &v) {this->xyzw_m128 = v.xyzw_m128;}
    Vector4() : xyzw_m128(_mm_set1_ps(0)) {}

    operator __m128() const {return this->xyzw_m128;}
    Vector4 operator=(const __m128 &v) {this->xyzw_m128 = v; return *this;}
    Vector4 operator=(const Vector4 &v) {*this = v; return *this;}

    __m128 operator+(const __m128 &rhs) { return _mm_add_ps(this->xyzw_m128, rhs); }
    __m128 operator-(const __m128 &rhs) { return _mm_sub_ps(this->xyzw_m128, rhs); }
    __m128 operator*(const __m128 &rhs) { return _mm_mul_ps(this->xyzw_m128, rhs); }
    __m128 operator/(const __m128 &rhs) { return _mm_div_ps(this->xyzw_m128, rhs); }
    __m128 operator+=(const __m128 &rhs) { this->xyzw_m128 = _mm_add_ps(this->xyzw_m128, rhs); return this->xyzw_m128; }
    __m128 operator-=(const __m128 &rhs) { this->xyzw_m128 = _mm_sub_ps(this->xyzw_m128, rhs); return this->xyzw_m128; }
    __m128 operator*=(const __m128 &rhs) { this->xyzw_m128 = _mm_mul_ps(this->xyzw_m128, rhs); return this->xyzw_m128; }
    __m128 operator/=(const __m128 &rhs) { this->xyzw_m128 = _mm_div_ps(this->xyzw_m128, rhs); return this->xyzw_m128; }
    
    __m128 operator+(const f32 &rhs) { return _mm_add_ps(this->xyzw_m128, _mm_set1_ps(rhs)); }
    __m128 operator-(const f32 &rhs) { return _mm_sub_ps(this->xyzw_m128, _mm_set1_ps(rhs)); }
    __m128 operator*(const f32 &rhs) { return _mm_mul_ps(this->xyzw_m128, _mm_set1_ps(rhs)); }
    __m128 operator/(const f32 &rhs) { return _mm_div_ps(this->xyzw_m128, _mm_set1_ps(rhs)); }
    __m128 operator+=(const f32 &rhs) { this->xyzw_m128 = _mm_add_ps(this->xyzw_m128, _mm_set1_ps(rhs)); return this->xyzw_m128; }
    __m128 operator-=(const f32 &rhs) { this->xyzw_m128 = _mm_sub_ps(this->xyzw_m128, _mm_set1_ps(rhs)); return this->xyzw_m128; }
    __m128 operator*=(const f32 &rhs) { this->xyzw_m128 = _mm_mul_ps(this->xyzw_m128, _mm_set1_ps(rhs)); return this->xyzw_m128; }
    __m128 operator/=(const f32 &rhs) { this->xyzw_m128 = _mm_div_ps(this->xyzw_m128, _mm_set1_ps(rhs)); return this->xyzw_m128; }
    
    static f32 Dot(const __m128 &lhs, const __m128 &rhs) { return hadd_sse3(_mm_mul_ps(lhs, rhs)); }
    static __m128 Cross(const __m128 &a, const __m128 &b)
    {
        return _mm_sub_ps(
            _mm_mul_ps(
                _mm_shuffle_ps(a,a,_MM_SHUFFLE(0,0,2,1)),
                _mm_shuffle_ps(b,b,_MM_SHUFFLE(0,1,0,2))
            ), 
            _mm_mul_ps(
                _mm_shuffle_ps(a,a,_MM_SHUFFLE(0,1,0,2)),
                _mm_shuffle_ps(b,b,_MM_SHUFFLE(0,0,2,1))
            )
        );
    }

    f32 Length(){ return sqrt(Dot(this->xyzw_m128, this->xyzw_m128)); }
    f32 LengthSquared(){ return Dot(this->xyzw_m128, this->xyzw_m128); }
    __m128 Normalize() 
    {
        __m128 v = _mm_mul_ps(this->xyzw_m128, this->xyzw_m128);
        __m128 shuf = _mm_movehdup_ps(v);
        __m128 sums = _mm_add_ps(v, shuf);
        shuf = _mm_movehl_ps(sums, sums);
        sums = _mm_add_ps(shuf, sums);
        
        sums = _mm_rsqrt_ss(sums);
        return _mm_mul_ps(
            _mm_shuffle_ps(sums,sums,_MM_SHUFFLE(0,0,0,0)),
            v
        );
    }

    static f32 Length(const __m128 &v){ return sqrt(Dot(v,v)); }
    static f32 LengthSquared(const __m128 &v){ return Dot(v,v); }
    static __m128 Normalize(const __m128 &vin) 
    {
        __m128 v = _mm_mul_ps(vin, vin);
        __m128 shuf = _mm_movehdup_ps(v);
        __m128 sums = _mm_add_ps(v, shuf);
        shuf = _mm_movehl_ps(sums, sums);
        sums = _mm_add_ps(shuf, sums);
        
        sums = _mm_rsqrt_ss(sums);
        return _mm_mul_ps(
            _mm_shuffle_ps(sums,sums,_MM_SHUFFLE(0,0,0,0)),
            v
        );
    }

    static __m128 Lerp(const __m128 &a, const __m128 &b, const f32 t)
    {
        return _mm_add_ps(a,_mm_mul_ps(_mm_set1_ps(t), _mm_sub_ps(b,a)));
    }

    static __m128 SmoothDamp(   const __m128 &from, 
                                const __m128 &to, 
                                __m128 &vel, 
                                f32 smoothTime, 
                                f32 deltaTime)
    {
        f32 omega = 2.0f/smoothTime;
        f32 x = omega*deltaTime;
        f32 exp = 1.0f/(1.0f+x+0.48f*x*x+0.235f*x*x*x);
        Vector4 change = from - to;
        Vector4 temp = (vel+change*omega)*deltaTime;
        vel = (vel-temp*omega)*exp;
        return to+(change+temp)*exp;
    }

    f32 GetXYZW(Vector4_Component c)
    {
        f32 xyzw[4] = {};
        _mm_store_ps(xyzw, this->xyzw_m128);
        return xyzw[c];
    }

    static void GetXYZW(const __m128 &v, f32 xyzw[4]) { _mm_store_ps(xyzw, v); }
    void GetXYZW(f32 v[4]) { _mm_store_ps(v, this->xyzw_m128); }

};

struct Matrix4x4
{
    union
    {
        struct {__m128 c0, c1, c2, c3;};
        float ij[16];
    };
    Matrix4x4() : 
        c0(_mm_set1_ps(0)), 
        c1(_mm_set1_ps(0)), 
        c2(_mm_set1_ps(0)), 
        c3(_mm_set1_ps(0)) {}
    Matrix4x4(const Vector4 &a, const Vector4 &b, const Vector4 &c, const Vector4 &d) : 
        c0(a.xyzw_m128), 
        c1(b.xyzw_m128), 
        c2(c.xyzw_m128), 
        c3(d.xyzw_m128) {}
    Matrix4x4(const f32 a[], const f32 b[], const f32 c[], const f32 d[]) :
        c0(_mm_load_ps(a)),
        c1(_mm_load_ps(b)),
        c2(_mm_load_ps(c)),
        c3(_mm_load_ps(d)) {}
    Matrix4x4(const __m128 &a, const __m128 &b, const __m128 &c, const __m128 &d) :
        c0(a), c1(b), c2(c), c3(d) {}
    Matrix4x4(  const f32 _00, const f32 _01, const f32 _02, const f32 _03, 
                const f32 _10, const f32 _11, const f32 _12, const f32 _13, 
                const f32 _20, const f32 _21, const f32 _22, const f32 _23, 
                const f32 _30, const f32 _31, const f32 _32, const f32 _33)
    {
        this->c0 = _mm_set_ps(_30, _20, _10, _00);
        this->c1 = _mm_set_ps(_31, _21, _11, _01);
        this->c2 = _mm_set_ps(_32, _22, _12, _02);
        this->c3 = _mm_set_ps(_33, _23, _13, _03);
    }

    Matrix4x4 operator+(const Matrix4x4 &m){ return Matrix4x4(_mm_add_ps(m.c0, this->c0),_mm_add_ps(m.c1, this->c1),_mm_add_ps(m.c2, this->c2),_mm_add_ps(m.c3, this->c3)); }
    Matrix4x4 operator-(const Matrix4x4 &m){ return Matrix4x4(_mm_sub_ps(m.c0, this->c0),_mm_sub_ps(m.c1, this->c1),_mm_sub_ps(m.c2, this->c2),_mm_sub_ps(m.c3, this->c3)); }
    Matrix4x4 operator+=(const Matrix4x4 &m){ *this = Matrix4x4(_mm_add_ps(m.c0, this->c0),_mm_add_ps(m.c1, this->c1),_mm_add_ps(m.c2, this->c2),_mm_add_ps(m.c3, this->c3)); return *this; }
    Matrix4x4 operator-=(const Matrix4x4 &m){ *this = Matrix4x4(_mm_sub_ps(m.c0, this->c0),_mm_sub_ps(m.c1, this->c1),_mm_sub_ps(m.c2, this->c2),_mm_sub_ps(m.c3, this->c3)); return *this; }
    Matrix4x4 operator*(const float s) { __m128 v = _mm_set1_ps(s); return Matrix4x4(_mm_mul_ps(v, this->c0),_mm_mul_ps(v, this->c1),_mm_mul_ps(v, this->c2),_mm_mul_ps(v, this->c3)); }
    Matrix4x4 operator/(const float s) { __m128 v = _mm_set1_ps(1.0f/s); return Matrix4x4(_mm_mul_ps(v, this->c0),_mm_mul_ps(v, this->c1),_mm_mul_ps(v, this->c2),_mm_mul_ps(v, this->c3)); }

    Vector4 operator*(const __m128 &v) 
    {
        return _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(this->c0, _mm_shuffle_ps(v,v,_MM_SHUFFLE(0,0,0,0))),
                _mm_mul_ps(this->c1, _mm_shuffle_ps(v,v,_MM_SHUFFLE(1,1,1,1)))
            ),
            _mm_add_ps(
                _mm_mul_ps(this->c2, _mm_shuffle_ps(v,v,_MM_SHUFFLE(2,2,2,2))),
                _mm_mul_ps(this->c3, _mm_shuffle_ps(v,v,_MM_SHUFFLE(3,3,3,3)))
            )
        );
    }

    Matrix4x4 operator*(const Matrix4x4 &m)
    {
        __m128 c0 = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(0,0,0,0))),
                _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(1,1,1,1)))
            ),
            _mm_add_ps(
                _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(2,2,2,2))),
                _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(3,3,3,3)))
            )
        ); 
        __m128 c1 = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(0,0,0,0))),
                _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(1,1,1,1)))
            ),
            _mm_add_ps(
                _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(2,2,2,2))),
                _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(3,3,3,3)))
            )
        );
        __m128 c2 = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(0,0,0,0))),
                _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(1,1,1,1)))
            ),
            _mm_add_ps(
                _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(2,2,2,2))),
                _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(3,3,3,3)))
            )
        );
        __m128 c3 = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c3,m.c3,_MM_SHUFFLE(0,0,0,0))),
                _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c3,m.c3,_MM_SHUFFLE(1,1,1,1)))
            ),
            _mm_add_ps(
                _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c3,m.c3,_MM_SHUFFLE(2,2,2,2))),
                _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c3,m.c3,_MM_SHUFFLE(3,3,3,3)))
            )
        );

        return Matrix4x4(c0,c1,c2,c3);
    }

    Matrix4x4 operator*=(const Matrix4x4 &m)
    {
        __m128 c0 = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(0,0,0,0))),
                _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(1,1,1,1)))
            ),
            _mm_add_ps(
                _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(2,2,2,2))),
                _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(3,3,3,3)))
            )
        ); 
        __m128 c1 = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(0,0,0,0))),
                _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(1,1,1,1)))
            ),
            _mm_add_ps(
                _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(2,2,2,2))),
                _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(3,3,3,3)))
            )
        );
        __m128 c2 = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(0,0,0,0))),
                _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(1,1,1,1)))
            ),
            _mm_add_ps(
                _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(2,2,2,2))),
                _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(3,3,3,3)))
            )
        );
        __m128 c3 = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c3,m.c3,_MM_SHUFFLE(0,0,0,0))),
                _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c3,m.c3,_MM_SHUFFLE(1,1,1,1)))
            ),
            _mm_add_ps(
                _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c3,m.c3,_MM_SHUFFLE(2,2,2,2))),
                _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c3,m.c3,_MM_SHUFFLE(3,3,3,3)))
            )
        );

        this->c0 = c0;
        this->c1 = c1;
        this->c2 = c2;
        this->c3 = c3;
        return *this;
    }

    Matrix4x4 Transpose()
    {
        __m128 A = _mm_unpacklo_ps(this->c0,this->c1);
        __m128 B = _mm_unpackhi_ps(this->c0,this->c1);
        __m128 C = _mm_unpacklo_ps(this->c2,this->c3);
        __m128 D = _mm_unpackhi_ps(this->c2,this->c3);
        
        return Matrix4x4(
            _mm_movelh_ps(A,C),
            _mm_movehl_ps(C,A),
            _mm_movelh_ps(B,D),
            _mm_movehl_ps(D,B)
        );
    }

    static Matrix4x4 Transpose(const Matrix4x4 &m)
    {
        __m128 A = _mm_unpacklo_ps(m.c0,m.c1);
        __m128 B = _mm_unpackhi_ps(m.c0,m.c1);
        __m128 C = _mm_unpacklo_ps(m.c2,m.c3);
        __m128 D = _mm_unpackhi_ps(m.c2,m.c3);
        
        return Matrix4x4(
            _mm_movelh_ps(A,C),
            _mm_movehl_ps(C,A),
            _mm_movelh_ps(B,D),
            _mm_movehl_ps(D,B)
        );
    }

    static void Store(float a[16], const Matrix4x4 &m)
    {
        _mm_store_ps((a+0),  m.c0);
        _mm_store_ps((a+4),  m.c1);
        _mm_store_ps((a+8),  m.c2);
        _mm_store_ps((a+12), m.c3);
    }

    static Matrix4x4 Perspective(float fov, float ar, float near, float far)
    {
        fov = fov*PI/180.0f;
        float cot_fov = 1. / tan(fov*.5);
        float cot_fov_ar = cot_fov * (1./ar);
        float nf = 1. / (near - far);
        return Matrix4x4(
            cot_fov, 0, 0, 0,
            0, cot_fov, 0, 0,
            0, 0, (near+far)*nf, 2*near*far*nf,
            0, 0, -1, 0
        );
    }

    static Matrix4x4 LookAt(Vector4 camera, Vector4 eye, Vector4 up)
    {
        Vector4 z_axis = Vector4::Normalize(camera - eye);
        Vector4 x_axis = Vector4::Normalize(Vector4::Cross(up, z_axis));
        Vector4 y_axis = Vector4::Cross(z_axis, x_axis);

        return Matrix4x4
        (
            x_axis.xyzw[0], y_axis.xyzw[0], z_axis.xyzw[0], 0,
            x_axis.xyzw[1], y_axis.xyzw[1], z_axis.xyzw[1], 0,
            x_axis.xyzw[2], y_axis.xyzw[2], z_axis.xyzw[2], 0,
            -Vector4::Dot(x_axis, eye), -Vector4::Dot(y_axis, eye), -Vector4::Dot(z_axis, eye), 1
        );
    }
};
#pragma endregion

#pragma region [ File Loading ]
/*
    MdFile file = {};
    mdOpenFile("../shaders/test.vsh", file);
    
    usize size = file.size;
    void *buf = malloc(size);
    
    mdFileCopyToBuffer(file, buf);
    mdCloseFile(file);
*/
struct MdFile
{
    FILE *handle;
    usize size;
};

MdResult mdOpenFile(const char *p_filepath, const char *p_file_modes, MdFile &file)
{
    FILE *fp = fopen(p_filepath, p_file_modes);
    if (fp == NULL)
    {
        LOG_ERROR("failed to load file at path \"%s\"\n", p_filepath);
        return MD_ERROR_FILE_NOT_FOUND;
    }

    file.handle = fp;

    fseek(fp, 0, SEEK_END);
    file.size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    return MD_SUCCESS;
}

MdResult mdFileCopyToBuffer(MdFile file, void *p_data, int block_size = -1)
{
    block_size = (block_size > 0) ? block_size : file.size;
    usize block_count = ceil(file.size / block_size);
    usize current_size = file.size;

    for (usize b=0; b<block_count; b++)
    {
        usize copy_size = MIN_VAL(current_size, block_size);
        int result = fread(p_data, 1, copy_size, file.handle);
        if (result < copy_size)
        {
            LOG_ERROR("failed to read entire file: %s\n", strerror(errno));
            return MD_ERROR_FILE_READ_FAILURE;
        }

        current_size -= block_size;
    }

    return MD_SUCCESS;
}

void mdCloseFile(MdFile &file)
{
    fclose(file.handle);
}

#pragma endregion

#pragma region [ Window ]
enum MdWindowEvents
{
    MD_WINDOW_UNCHANGED = 0,
    MD_WINDOW_RESIZED = 1
};

struct MdWindow
{
    u16 w, h;
    char title[128];
    SDL_Window *window;
    MdWindowEvents event = MD_WINDOW_UNCHANGED;

    MdWindow(u16 w, u16 h) : w(w), h(h) {}
    MdWindow(u16 w, u16 h, const char *title) : w(w), h(h) 
    {
        usize len = MIN_VAL(128, strlen(title) + 1);
        memcpy((void*)this->title, title, len - 1);
        this->title[len-1] = '\0';
    }
    MdWindow() : w(1920), h(1080) {}
};

MdResult mdCreateWindow(u16 w, u16 h, const char *title, MdWindow &window)
{
    // Create window
    window = MdWindow(w, h, title);
    window.window = SDL_CreateWindow(window.title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_VULKAN);
    if (window.window == NULL)
    {
        LOG_ERROR("failed to create SDL window: %s\n", SDL_GetError());
        return MD_ERROR_WINDOW_FAILURE;
    
    }
    int nw, nh;
    SDL_GetWindowSize(window.window, &nw, &nh);
    window.w = nw;
    window.h = nh;
    return MD_SUCCESS;
}

void mdDestroyWindow(MdWindow &window)
{
    if (window.window != NULL)
        SDL_DestroyWindow(window.window);
}

void mdGetWindowSurface(const MdWindow &window, VkInstance instance, VkSurfaceKHR *surface)
{
    if (SDL_Vulkan_CreateSurface(window.window, instance, surface) != SDL_TRUE)
    {
        LOG_ERROR("failed to create window surface: %s\n", SDL_GetError());
        surface = VK_NULL_HANDLE;
    }
}
#pragma endregion

#pragma region [ Render Context ]
struct MdGPUTexture
{
    VmaAllocation allocation;
    VmaAllocationInfo allocation_info;
    VkFormat format;
    VkImage image;
    VkImageView image_view;
    VkSampler sampler;
    VkImageSubresourceRange subresource;
    u16 w, h;
    u16 pitch;
};

struct MdRenderContext
{
    u32 api_version;
    vkb::Instance instance;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::Device device;

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    vkb::Swapchain swapchain;

    std::vector<VkImageView> sw_image_views;
    std::vector<VkImage> sw_images;
};

struct MdRenderQueue
{
    VkQueue queue_handle;
    i32 queue_index;

    MdRenderQueue() : queue_handle(VK_NULL_HANDLE), queue_index(-1) {}
    MdRenderQueue(VkQueue handle, i32 index) : queue_handle(handle), queue_index(index) {}
};

MdResult mdInitContext(MdRenderContext &context, const std::vector<const char*> &instance_extensions)
{
    // Create the vulkan instance
    vkb::InstanceBuilder instance_builder;
    auto ret_instance = instance_builder
        .enable_extensions(instance_extensions)
        .require_api_version(VK_API_VERSION_1_3)
        .request_validation_layers()
        .use_default_debug_messenger()
        .build();
    if (!ret_instance)
    {
        LOG_ERROR("failed to create vulkan instance: %s\n", ret_instance.error().message().c_str());
        return MD_ERROR_VULKAN_INSTANCE_FAILURE;
    }
    context.instance = ret_instance.value();
    context.api_version = VK_API_VERSION_1_3;

    return MD_SUCCESS;
}

void mdDestroyContext(MdRenderContext &context)
{
    if (context.sw_image_views.size() > 0)
    {
        for (u32 i=0; i<context.sw_image_views.size(); i++)
        {
            if (context.sw_image_views[i] != VK_NULL_HANDLE)
                vkDestroyImageView(context.device, context.sw_image_views[i], NULL);
        }
    }

    vkb::destroy_swapchain(context.swapchain);
    vkb::destroy_device(context.device);
    if (context.surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(context.instance, context.surface, NULL);
    vkb::destroy_instance(context.instance);
}

MdResult mdCreateDevice(MdRenderContext &context)
{
    vkb::PhysicalDeviceSelector device_selector(context.instance);
    auto pdev_ret = device_selector
        .set_surface(context.surface)
        .set_minimum_version(1, 1)
        .select();
    
    if (!pdev_ret)
    {
        LOG_ERROR("failed to get physical device: %s\n", pdev_ret.error().message().c_str());
        return MD_ERROR_VULKAN_PHYSICAL_DEVICE_FAILURE;
    }
    vkb::DeviceBuilder device_builder(pdev_ret.value());
    auto dev_ret = device_builder.build();
    if (!dev_ret)
    {
        LOG_ERROR("failed to create logical device: %s\n", dev_ret.error().message().c_str());
        return MD_ERROR_VULKAN_LOGICAL_DEVICE_FAILURE;
    }
    context.physical_device = pdev_ret.value();
    context.device = dev_ret.value();

    return MD_SUCCESS;
}

MdResult mdGetQueue(VkQueueFlagBits queue_type, MdRenderContext &context, MdRenderQueue &queue)
{
    vkb::QueueType type;
    switch (queue_type)
    {
        case VK_QUEUE_GRAPHICS_BIT: type = vkb::QueueType::graphics; break;
        case VK_QUEUE_COMPUTE_BIT: type = vkb::QueueType::compute; break;
        case VK_QUEUE_TRANSFER_BIT: type = vkb::QueueType::transfer; break;
        default: LOG_ERROR("queue type is unsupported\n"); return MD_ERROR_VULKAN_QUEUE_NOT_PRESENT;
    }
    
    auto queue_ret = context.device.get_queue(type);
    if (!queue_ret)
    {
        LOG_ERROR("failed to get queue: %s\n", queue_ret.error().message().c_str());
        return MD_ERROR_VULKAN_QUEUE_NOT_PRESENT;
    }
    i32 index = context.device.get_queue_index(type).value();
    
    queue = MdRenderQueue(queue_ret.value(), index);
    return MD_SUCCESS;
}

MdResult mdGetSwapchain(MdRenderContext &context, bool rebuild = false)
{
    // Get swapchain
    vkb::SwapchainBuilder sw_builder(context.device);
    auto sw_ret = (rebuild) ? sw_builder.set_old_swapchain(context.swapchain).build() : sw_builder.build();
    
    if (!sw_ret)
    {
        LOG_ERROR("failed to get swapchain: %s\n", sw_ret.error().message().c_str());
        return MD_ERROR_VULKAN_SWAPCHAIN_FAILURE;
    }
    context.swapchain = sw_ret.value();
    
    // Create image views and framebuffers
    auto images = context.swapchain.get_images();
    if (!images)
    {
        LOG_ERROR("failed to get image views: %s\n", images.error().message().c_str());
        return MD_ERROR_VULKAN_SWAPCHAIN_IMAGE_VIEW_FAILURE;
    }

    auto views = context.swapchain.get_image_views();
    if (!views)
    {
        LOG_ERROR("failed to get image views: %s\n", views.error().message().c_str());
        return MD_ERROR_VULKAN_SWAPCHAIN_IMAGE_VIEW_FAILURE;
    }

    context.sw_images = images.value();
    context.sw_image_views = views.value();

    return MD_SUCCESS;
}

//VkResult mdRebuildSwapchain(MdRenderContext &context, u16 w, u16 h)
//{
//    VkResult result = VK_ERROR_UNKNOWN;
//
//    if (context.swapchain.swapchain != VK_NULL_HANDLE)
//        vkb::destroy_swapchain(context.swapchain);
//
//    mdGetSwapchain(context);
//
//    return result;
//}
#pragma endregion

#pragma region [ Command Encoder ]
struct MdCommandEncoder
{
    std::vector<VkCommandBuffer> buffers;
    VkCommandPool pool;
};

VkResult mdCreateCommandEncoder(MdRenderContext &context, u32 queue_family_index, MdCommandEncoder &encoder, VkCommandPoolCreateFlags flags = 0)
{
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = flags;
    pool_info.queueFamilyIndex = queue_family_index;
    VkResult result = vkCreateCommandPool(context.device, &pool_info, NULL, &encoder.pool);
    VK_CHECK(result, "failed to create command pool");
    
    return result;
}

VkResult mdAllocateCommandBuffers(MdRenderContext &context, u32 buffer_count, VkCommandBufferLevel level, MdCommandEncoder &encoder)
{
    VkCommandBufferAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandBufferCount = buffer_count;
    alloc_info.commandPool =  encoder.pool;
    alloc_info.level = level;
    encoder.buffers.reserve(buffer_count);

    VkResult result = vkAllocateCommandBuffers(context.device, &alloc_info, encoder.buffers.data());
    VK_CHECK(result, "failed to allocate command buffers");

    return result;
}

void mdDestroyCommandEncoder(MdRenderContext &context, MdCommandEncoder &encoder)
{
    if (encoder.buffers.size() > 0)
        vkFreeCommandBuffers(context.device, encoder.pool, encoder.buffers.size(), encoder.buffers.data());

    vkDestroyCommandPool(context.device, encoder.pool, NULL);
}

#pragma endregion

#pragma region [ Memory ]

struct MdGPUBuffer
{
    VmaAllocation allocation;
    VmaAllocationInfo allocation_info;
    VkBuffer buffer;
    u32 size;
};

struct MdGPUAllocator
{
    VmaAllocator allocator;
    MdGPUBuffer staging_buffer;
    MdRenderQueue queue;
};

VkResult mdCreateGPUAllocator(MdRenderContext &context, MdGPUAllocator &allocator, MdRenderQueue queue, VkDeviceSize staging_buffer_size = 1024*1024)
{
    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = context.instance;
    alloc_info.device = context.device;
    alloc_info.physicalDevice = context.physical_device;
    alloc_info.pAllocationCallbacks = NULL;
    alloc_info.pDeviceMemoryCallbacks = NULL;
    alloc_info.vulkanApiVersion = context.api_version;
    VkResult result = vmaCreateAllocator(&alloc_info, &allocator.allocator);
    VK_CHECK(result, "failed to create memory allocator");

    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.flags = 0;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.size = staging_buffer_size;
    
    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    result = vmaCreateBuffer(
        allocator.allocator, 
        &buffer_info, 
        &allocation_info, 
        &allocator.staging_buffer.buffer,
        &allocator.staging_buffer.allocation,
        &allocator.staging_buffer.allocation_info
    );
    VK_CHECK(result, "failed to allocate memory for staging buffer");

    allocator.queue = queue;
    allocator.staging_buffer.size = staging_buffer_size;
    return result;
}

VkResult mdAllocateGPUBuffer(VkBufferUsageFlags usage, u32 size, MdGPUAllocator &allocator, MdGPUBuffer &buffer)
{
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.flags = 0;
    buffer_info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.size = size;
    
    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateBuffer(
        allocator.allocator, 
        &buffer_info, 
        &allocation_info, 
        &buffer.buffer,
        &buffer.allocation,
        &buffer.allocation_info
    );
    VK_CHECK(result, "failed to allocate buffer");

    buffer.size = size;
    return result;
}

VkResult mdAllocateGPUUniformBuffer(u32 size, MdGPUAllocator &allocator, MdGPUBuffer &buffer)
{
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.flags = 0;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.size = size;
    
    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkResult result = vmaCreateBuffer(
        allocator.allocator, 
        &buffer_info, 
        &allocation_info, 
        &buffer.buffer,
        &buffer.allocation,
        &buffer.allocation_info
    );
    VK_CHECK(result, "failed to allocate buffer");

    buffer.size = size;
    return result;
}

void mdFreeGPUBuffer(MdGPUAllocator &allocator, MdGPUBuffer &buffer)
{
    vmaDestroyBuffer(allocator.allocator, buffer.buffer, buffer.allocation);
}

void mdFreeUniformBuffer(MdGPUAllocator &allocator, MdGPUBuffer &buffer)
{
    vmaUnmapMemory(allocator.allocator, buffer.allocation);
    vmaDestroyBuffer(allocator.allocator, buffer.buffer, buffer.allocation);
}

VkResult mdUploadToGPUBuffer(   MdRenderContext &context, 
                                MdGPUAllocator &allocator, 
                                u32 offset, 
                                u32 range, 
                                const void *p_data, 
                                MdGPUBuffer &buffer, 
                                MdCommandEncoder *p_command_encoder = NULL,
                                u32 command_buffer_index = 0)
{
    u32 size = range - offset;
    u32 block_size = allocator.staging_buffer.size;
    u32 block_count = (size / block_size) + 1;
    
    VkCommandBuffer cmd_buffer;
    VkResult result = VK_ERROR_UNKNOWN;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    // If the user did not pass their own command encoder, create our own temporary one
    bool active_recording = (p_command_encoder != NULL);
    if (!active_recording)
    {
        VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = allocator.queue.queue_index;
        
        result = vkCreateCommandPool(context.device, &pool_info, NULL, &cmd_pool);
        VK_CHECK(result, "failed to create command pool");

        VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_alloc_info.commandBufferCount = 1;
        cmd_alloc_info.commandPool = cmd_pool;
        cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        result = vkAllocateCommandBuffers(context.device, &cmd_alloc_info, &cmd_buffer);
        VK_CHECK(result, "failed to allocate command buffers");
    }
    else cmd_buffer = p_command_encoder->buffers[command_buffer_index];

    // Set up data pointers
    void *dst = allocator.staging_buffer.allocation_info.pMappedData; 
    u8 *data_ptr = (u8*)p_data;
    
    if (!active_recording)
    {
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buffer, &begin_info);
    }

    i32 current_size = size;
    for (u32 i=0; i<block_count; i++)
    {
        current_size = current_size - block_size*i;
        if (current_size < 0) 
            current_size = 0;
        
        memcpy(dst, (data_ptr+(size-current_size)), MIN_VAL(current_size, block_size));
        VkBufferCopy copy = {};
        copy.srcOffset = 0;
        copy.dstOffset = size - current_size;
        copy.size = MIN_VAL(current_size, block_size);
        
        vkCmdCopyBuffer(
            cmd_buffer, 
            allocator.staging_buffer.buffer, 
            buffer.buffer, 
            1,
            &copy
        );
    }

    if (!active_recording)
    {
        vkEndCommandBuffer(cmd_buffer);
        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer;
        
        vkQueueSubmit(allocator.queue.queue_handle, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(allocator.queue.queue_handle);

        vkFreeCommandBuffers(context.device, cmd_pool, 1, &cmd_buffer);
        vkDestroyCommandPool(context.device, cmd_pool, NULL);
    }

    return result;
}

VkResult mdUploadToUniformBuffer(   MdRenderContext &context, 
                                    MdGPUAllocator &allocator, 
                                    u32 offset, 
                                    u32 range, 
                                    const void *p_data, 
                                    MdGPUBuffer &buffer)
{
    u32 size = range - offset;
    if (size > (buffer.size))
    {
        LOG_ERROR(
            "the size of the memory region (%d) exceeds the size of the uniform buffer. (%d)\n",
            size,
            buffer.size
        );
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    
    void *p_mapped = NULL;
    VkResult result = vmaMapMemory(
        allocator.allocator, 
        buffer.allocation, 
        &p_mapped
    );
    VK_CHECK(result, "failed to map memory");

    memcpy(p_mapped, p_data, size);
    vmaUnmapMemory(allocator.allocator, buffer.allocation);
    return result;
}
#pragma endregion

#pragma region [ Textures (Related to memory) ]

u64 mdGetTextureSize(MdGPUTexture &texture)
{
    return texture.h * texture.pitch;
}

void mdTransitionImageLayout(   MdGPUTexture &texture, 
                                VkImageLayout src_layout, 
                                VkImageLayout dst_layout, 
                                VkAccessFlags src_access,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags src_stage,
                                VkPipelineStageFlags dst_stage,
                                VkCommandBuffer buffer)
{
    VkImageMemoryBarrier image_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    image_barrier.image = texture.image;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    
    image_barrier.oldLayout = src_layout;
    image_barrier.newLayout = dst_layout;
    image_barrier.srcAccessMask = src_access;
    image_barrier.dstAccessMask = dst_access;

    image_barrier.subresourceRange = texture.subresource;

    vkCmdPipelineBarrier(
        buffer, 
        src_stage, 
        dst_stage, 
        0, 
        0, 
        NULL, 
        0, 
        NULL, 
        1, 
        &image_barrier
    );
}

struct MdGPUTextureBuilder
{
    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkImageViewCreateInfo image_view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    u16 pitch;
};

void mdCreateTextureBuilder2D(MdGPUTextureBuilder &builder, u16 w, u16 h, VkFormat format, VkImageAspectFlags aspect, u16 pitch = 4)
{
    builder.image_info.flags = 0;
    builder.image_info.imageType = VK_IMAGE_TYPE_2D;
    builder.image_info.format = format;
    builder.image_info.extent = {w, h, 1};
    builder.image_info.mipLevels = 1;
    builder.image_info.arrayLayers = 1;
    builder.image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    builder.image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    builder.image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    builder.image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    builder.image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    
    builder.image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    builder.image_view_info.subresourceRange.aspectMask = aspect;
    builder.image_view_info.subresourceRange.baseArrayLayer = 0;
    builder.image_view_info.subresourceRange.baseMipLevel = 0;
    builder.image_view_info.subresourceRange.layerCount = 1;
    builder.image_view_info.subresourceRange.levelCount = 1;
    builder.image_view_info.format = format;
    builder.image_view_info.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY, 
        VK_COMPONENT_SWIZZLE_IDENTITY, 
        VK_COMPONENT_SWIZZLE_IDENTITY, 
        VK_COMPONENT_SWIZZLE_IDENTITY
    };
    builder.image_view_info.flags = 0;
    builder.image_view_info.image = VK_NULL_HANDLE;
    
    builder.sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    builder.sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    builder.sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    builder.sampler_info.anisotropyEnable = VK_FALSE;
    builder.sampler_info.maxAnisotropy = 1;
    builder.sampler_info.unnormalizedCoordinates = VK_FALSE;
    builder.sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    builder.sampler_info.compareEnable = VK_FALSE;
    builder.sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    builder.sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    builder.sampler_info.mipLodBias = 0;
    builder.sampler_info.minLod = 0;
    builder.sampler_info.maxLod = 0;
    builder.sampler_info.magFilter = VK_FILTER_LINEAR;
    builder.sampler_info.minFilter = VK_FILTER_LINEAR;

    builder.pitch = pitch;
}

VkResult mdBuildTexture2D(  MdRenderContext &context, 
                            MdGPUTextureBuilder &tex_builder,
                            MdGPUAllocator &allocator,
                            MdGPUTexture &texture, 
                            const void *data,
                            MdCommandEncoder *p_command_encoder = NULL,
                            u32 command_buffer_index = 0)
{
    texture.pitch = tex_builder.pitch;
    texture.w = tex_builder.image_info.extent.width;
    texture.h = tex_builder.image_info.extent.height;
    texture.format = tex_builder.image_info.format;
    texture.subresource = tex_builder.image_view_info.subresourceRange;

    VmaAllocationCreateInfo img_info = {};
    img_info.usage = VMA_MEMORY_USAGE_AUTO;
    img_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateImage(
        allocator.allocator, 
        &tex_builder.image_info, 
        &img_info, 
        &texture.image, 
        &texture.allocation, 
        &texture.allocation_info
    );
    VK_CHECK(result, "failed to create image allocation");
    
    tex_builder.image_view_info.image = texture.image;
    result = vkCreateImageView(context.device, &tex_builder.image_view_info, NULL, &texture.image_view);
    VK_CHECK(result, "failed to create texture image view");

    result = vkCreateSampler(context.device, &tex_builder.sampler_info, NULL, &texture.sampler);
    VK_CHECK(result, "failed to create texture image sampler");

    MdGPUBuffer image_staging_buffer = {};
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.size = mdGetTextureSize(texture);

    VmaAllocationCreateInfo buf_alloc_info = {};
    buf_alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    buf_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    result = vmaCreateBuffer(
        allocator.allocator, 
        &buffer_info, 
        &buf_alloc_info, 
        &image_staging_buffer.buffer, 
        &image_staging_buffer.allocation, 
        &image_staging_buffer.allocation_info
    );
    VK_CHECK(result, "failed to create image staging buffer");

    MdCommandEncoder encoder = {};
    VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
    if (p_command_encoder == NULL)
    {
        result = mdCreateCommandEncoder(
            context, 
            allocator.queue.queue_index, 
            encoder,
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
        );
        VK_CHECK(result, "failed to create command encoder");

        result = mdAllocateCommandBuffers(context, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, encoder);
        VK_CHECK(result, "failed to allocate command buffers");
        cmd_buffer = encoder.buffers[0];
    
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.pInheritanceInfo = NULL;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buffer, &begin_info);
    }
    else cmd_buffer = p_command_encoder->buffers[command_buffer_index];

    void *dst_ptr = image_staging_buffer.allocation_info.pMappedData;
    memcpy(dst_ptr, data, buffer_info.size);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageOffset = {0,0,0};
    region.imageExtent = {texture.w, texture.h, 1};
    region.imageSubresource.aspectMask = tex_builder.image_view_info.subresourceRange.aspectMask;
    region.imageSubresource.baseArrayLayer = tex_builder.image_view_info.subresourceRange.baseArrayLayer;
    region.imageSubresource.layerCount = tex_builder.image_view_info.subresourceRange.layerCount;
    region.imageSubresource.mipLevel = tex_builder.image_view_info.subresourceRange.baseMipLevel;
    
    mdTransitionImageLayout(
        texture, 
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        cmd_buffer
    );
    vkCmdCopyBufferToImage(
        cmd_buffer, 
        image_staging_buffer.buffer, 
        texture.image, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        1, 
        &region
    );
    mdTransitionImageLayout(
        texture, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        cmd_buffer
    );

    if (p_command_encoder == NULL)
    {
        vkEndCommandBuffer(cmd_buffer);

        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer;

        vkQueueSubmit(allocator.queue.queue_handle, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(allocator.queue.queue_handle);

        mdDestroyCommandEncoder(context, encoder);    
    }
    
    vmaDestroyBuffer(allocator.allocator, image_staging_buffer.buffer, image_staging_buffer.allocation);

    return result;
}

VkResult mdBuildDepthTexture2D( MdRenderContext &context, 
                                MdGPUTextureBuilder &tex_builder,
                                MdGPUAllocator &allocator,
                                MdGPUTexture &texture, 
                                MdCommandEncoder *p_command_encoder = NULL,
                                u32 command_buffer_index = 0)
{
    texture.pitch = tex_builder.pitch;
    texture.w = tex_builder.image_info.extent.width;
    texture.h = tex_builder.image_info.extent.height;
    texture.format = tex_builder.image_info.format;
    texture.subresource = tex_builder.image_view_info.subresourceRange;
    
    tex_builder.image_info.extent.width = 8192;
    tex_builder.image_info.extent.height = 8192;

    VmaAllocationCreateInfo img_info = {};
    img_info.usage = VMA_MEMORY_USAGE_AUTO;
    img_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateImage(
        allocator.allocator, 
        &tex_builder.image_info, 
        &img_info, 
        &texture.image, 
        &texture.allocation, 
        &texture.allocation_info
    );
    VK_CHECK(result, "failed to create image allocation");
    
    tex_builder.image_view_info.image = texture.image;
    result = vkCreateImageView(context.device, &tex_builder.image_view_info, NULL, &texture.image_view);
    VK_CHECK(result, "failed to create texture image view");

    result = vkCreateSampler(context.device, &tex_builder.sampler_info, NULL, &texture.sampler);
    VK_CHECK(result, "failed to create texture image sampler");

    MdCommandEncoder encoder = {};
    VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
    if (p_command_encoder == NULL)
    {
        result = mdCreateCommandEncoder(
            context, 
            allocator.queue.queue_index, 
            encoder,
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
        );
        VK_CHECK(result, "failed to create command encoder");

        result = mdAllocateCommandBuffers(context, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, encoder);
        VK_CHECK(result, "failed to allocate command buffers");
        cmd_buffer = encoder.buffers[0];
    
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.pInheritanceInfo = NULL;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buffer, &begin_info);
    }
    else cmd_buffer = p_command_encoder->buffers[command_buffer_index];
    
    mdTransitionImageLayout(
        texture, 
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        cmd_buffer
    );
    
    if (p_command_encoder == NULL)
    {
        vkEndCommandBuffer(cmd_buffer);

        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer;

        vkQueueSubmit(allocator.queue.queue_handle, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(allocator.queue.queue_handle);

        mdDestroyCommandEncoder(context, encoder);    
    }
    
    return result;
}

VkResult mdBuildColorAttachmentTexture2D(   MdRenderContext &context, 
                                            MdGPUTextureBuilder &tex_builder,
                                            MdGPUAllocator &allocator,
                                            MdGPUTexture &texture, 
                                            MdCommandEncoder *p_command_encoder = NULL,
                                            u32 command_buffer_index = 0)
{
    texture.pitch = tex_builder.pitch;
    texture.w = tex_builder.image_info.extent.width;
    texture.h = tex_builder.image_info.extent.height;
    texture.format = tex_builder.image_info.format;
    texture.subresource = tex_builder.image_view_info.subresourceRange;
    
    //tex_builder.image_info.extent.width = 8192;
    //tex_builder.image_info.extent.height = 8192;

    VmaAllocationCreateInfo img_info = {};
    img_info.usage = VMA_MEMORY_USAGE_AUTO;
    img_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateImage(
        allocator.allocator, 
        &tex_builder.image_info, 
        &img_info, 
        &texture.image, 
        &texture.allocation, 
        &texture.allocation_info
    );
    VK_CHECK(result, "failed to create image allocation");
    
    tex_builder.image_view_info.image = texture.image;
    result = vkCreateImageView(context.device, &tex_builder.image_view_info, NULL, &texture.image_view);
    VK_CHECK(result, "failed to create texture image view");

    result = vkCreateSampler(context.device, &tex_builder.sampler_info, NULL, &texture.sampler);
    VK_CHECK(result, "failed to create texture image sampler");

    MdCommandEncoder encoder = {};
    VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
    if (p_command_encoder == NULL)
    {
        result = mdCreateCommandEncoder(
            context, 
            allocator.queue.queue_index, 
            encoder,
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
        );
        VK_CHECK(result, "failed to create command encoder");

        result = mdAllocateCommandBuffers(context, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, encoder);
        VK_CHECK(result, "failed to allocate command buffers");
        cmd_buffer = encoder.buffers[0];
    
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.pInheritanceInfo = NULL;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buffer, &begin_info);
    }
    else cmd_buffer = p_command_encoder->buffers[command_buffer_index];
    
    mdTransitionImageLayout(
        texture, 
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        cmd_buffer
    );
    
    if (p_command_encoder == NULL)
    {
        vkEndCommandBuffer(cmd_buffer);

        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer;

        vkQueueSubmit(allocator.queue.queue_handle, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(allocator.queue.queue_handle);

        mdDestroyCommandEncoder(context, encoder);    
    }
    
    return result;
}

void mdSetTextureUsage(MdGPUTextureBuilder &builder, VkImageUsageFlags usage, VkImageAspectFlagBits image_aspect)
{
    builder.image_view_info.subresourceRange.aspectMask = image_aspect;
    builder.image_info.usage = usage;
}

void mdSetFilterWrap(   MdGPUTextureBuilder &builder, 
                        VkSamplerAddressMode mode_u, 
                        VkSamplerAddressMode mode_v, 
                        VkSamplerAddressMode mode_w)
{
    builder.sampler_info.addressModeU = mode_u;
    builder.sampler_info.addressModeV = mode_v;
    builder.sampler_info.addressModeW = mode_w;
}

void mdSetMipmapOptions(MdGPUTextureBuilder &builder, VkSamplerMipmapMode mode)
{
    builder.sampler_info.mipmapMode = mode;
}

void mdSetMagFilters(MdGPUTextureBuilder &builder, VkFilter mag_filter, VkFilter min_filter)
{
    builder.sampler_info.magFilter = mag_filter;
    builder.sampler_info.minFilter = min_filter;
}

void mdDestroyTexture(MdGPUAllocator &allocator, MdGPUTexture &texture)
{
    if (texture.sampler != NULL)
        vkDestroySampler(allocator.allocator->m_hDevice, texture.sampler, NULL);
    if (texture.image_view != NULL)
        vkDestroyImageView(allocator.allocator->m_hDevice, texture.image_view, NULL);
    vmaDestroyImage(allocator.allocator, texture.image, texture.allocation);
}

void mdDestroyGPUAllocator(MdGPUAllocator &allocator)
{
    vmaDestroyBuffer(
        allocator.allocator, 
        allocator.staging_buffer.buffer, 
        allocator.staging_buffer.allocation
    );
    vmaDestroyAllocator(allocator.allocator);
}

#pragma endregion

#pragma region [ Render Pass ]
struct MdRenderTargetBuilder
{
    std::vector<VkAttachmentReference> color_references;
    std::vector<VkAttachmentReference> depth_references;
    std::vector<VkAttachmentDescription> descriptions;
    std::vector<VkSubpassDependency> subpass_dependencies;

    std::vector<VkImageView> color_views;
    std::vector<VkImageView> depth_views;
    
    u16 w, h;
};

struct MdRenderTarget
{
    VkRenderPass pass;
    std::vector<VkFramebuffer> buffers;

    u16 w, h;
};

void mdCreateRenderTargetBuilder(MdRenderContext &context, u16 w, u16 h, MdRenderTargetBuilder &target)
{
    target.w = w;
    target.h = h;
}

void mdRenderTargetAddColorAttachment(  MdRenderTargetBuilder &builder, 
                                        VkFormat image_format, 
                                        VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED, 
                                        VkImageLayout final_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
{
    VkAttachmentDescription color_att = {};
    VkAttachmentReference color_ref = {};
    VkSubpassDependency color_deps = {};
    {
        color_att.format = image_format;
        color_att.samples = VK_SAMPLE_COUNT_1_BIT;
        color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_att.initialLayout = initial_layout;
        color_att.finalLayout = final_layout;

        color_ref.attachment = builder.descriptions.size();
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
        color_deps.srcSubpass = VK_SUBPASS_EXTERNAL;
        color_deps.dstSubpass = 0;
        color_deps.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        color_deps.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        color_deps.srcAccessMask = 0;
        color_deps.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    builder.descriptions.push_back(color_att);
    builder.color_references.push_back(color_ref);
    builder.subpass_dependencies.push_back(color_deps);
}

void mdRenderTargetAddDepthAttachment(  MdRenderTargetBuilder &builder, 
                                        VkFormat image_format, 
                                        VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED, 
                                        VkImageLayout final_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
{
    VkAttachmentDescription depth_att = {};
    VkAttachmentReference depth_ref = {};
    VkSubpassDependency depth_deps = {};
    {
        depth_att.format = image_format;
        depth_att.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_att.initialLayout = initial_layout;
        depth_att.finalLayout = final_layout;

        depth_ref.attachment = builder.descriptions.size();
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
        depth_deps.srcSubpass = VK_SUBPASS_EXTERNAL;
        depth_deps.dstSubpass = 0;
        depth_deps.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depth_deps.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depth_deps.srcAccessMask = 0;
        depth_deps.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    builder.descriptions.push_back(depth_att);
    builder.depth_references.push_back(depth_ref);
    builder.subpass_dependencies.push_back(depth_deps);
}

VkResult mdBuildRenderTarget(MdRenderContext &context, MdRenderTargetBuilder &builder, MdRenderTarget &target)
{
    target.w = builder.w;
    target.h = builder.h;
    
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.flags = 0;
    subpass.colorAttachmentCount = builder.color_references.size();
    subpass.pColorAttachments = builder.color_references.data();
    subpass.pDepthStencilAttachment = builder.depth_references.data();
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = NULL;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = NULL;
    subpass.pResolveAttachments = NULL;

    VkRenderPassCreateInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = builder.subpass_dependencies.size();
    rp_info.pDependencies = builder.subpass_dependencies.data();
    rp_info.attachmentCount = builder.descriptions.size();
    rp_info.pAttachments = builder.descriptions.data();
    rp_info.flags = 0;

    VkResult result = vkCreateRenderPass(context.device, &rp_info, NULL, &target.pass);
    VK_CHECK(result, "failed to create renderpass");

    return result;
}

VkResult mdRenderTargetAddFramebuffer(MdRenderContext &context, MdRenderTarget &target, const std::vector<VkImageView> &views)
{
    VkFramebufferCreateInfo fb_info {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.attachmentCount = 1;
    fb_info.width = target.w;
    fb_info.height = target.h;
    fb_info.layers = 1;
    fb_info.renderPass = target.pass;
    fb_info.attachmentCount = views.size();
    fb_info.pAttachments = views.data();

    VkFramebuffer handle = VK_NULL_HANDLE;

    VkResult result = vkCreateFramebuffer(context.device, &fb_info, NULL, &handle);
    VK_CHECK(result, "failed to create framebuffer");

    target.buffers.push_back(handle);
    return result;
}

void mdDestroyRenderTarget(MdRenderContext &context, MdRenderTarget &target)
{
    if (target.buffers.size() > 0)
    {
        for (u32 i=0; i<target.buffers.size(); i++)
            vkDestroyFramebuffer(context.device, target.buffers[i], NULL);
            
        target.buffers.clear();
    }
    vkDestroyRenderPass(context.device, target.pass, NULL);
}
#pragma endregion

#pragma region [ Shader Modules and Descriptors ]

struct MdShaderSource
{
    std::vector<VkPipelineShaderStageCreateInfo> modules;
};

VkResult mdLoadShaderSPIRV( MdRenderContext &context, 
                            u32 code_size, 
                            const u32 *p_code, 
                            VkShaderStageFlagBits stage,
                            MdShaderSource &source)
{
    VkPipelineShaderStageCreateInfo stage_info = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage_info.pSpecializationInfo = NULL; // TO-DO: worry about this later
    stage_info.pName = "main";
    stage_info.flags = 0;
    stage_info.stage = stage;

    VkShaderModuleCreateInfo module_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module_info.codeSize = code_size;
    module_info.pCode = p_code;
    module_info.flags = 0;
    VkResult result = vkCreateShaderModule(context.device, &module_info, NULL, &stage_info.module);
    VK_CHECK(result, "failed to create shader module");

    source.modules.push_back(stage_info);
    return result;
}

VkResult mdLoadShaderSPIRVFromFile( MdRenderContext &context, 
                                    const char *p_filepath,
                                    VkShaderStageFlagBits stage,
                                    MdShaderSource &source)
{
    MdFile file = {};
    MdResult md_result = mdOpenFile(p_filepath, "r", file);
    MD_CHECK_ANY(md_result, VK_ERROR_UNKNOWN, "failed to load file");

    u32 *code = (u32*)malloc(file.size);
    u32 size = (file.size / 4) * 4;

    md_result = mdFileCopyToBuffer(file, code);
    MD_CHECK_ANY(md_result, VK_ERROR_UNKNOWN, "failed to copy file to memory");

    mdCloseFile(file);

    VkResult result = mdLoadShaderSPIRV(context, size, code, stage, source);
    free(code);

    return result;
}

void mdDestroyShaderSource(MdRenderContext &context, MdShaderSource &source)
{
    if (source.modules.size() > 0)
    {
        for (u32 i=0; i<source.modules.size(); i++)
        {
            if (source.modules[i].module != VK_NULL_HANDLE)
                vkDestroyShaderModule(context.device, source.modules[i].module, NULL);
        }
    }
}

struct MdDescriptorSetAllocator
{
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorSet> sets; 

    u32 max_sets, max_bindings;
};

void mdCreateDescriptorAllocator(u32 max_sets, u32 max_bindings, MdDescriptorSetAllocator &allocator)
{
    allocator.max_sets = max_sets;
    allocator.max_bindings = max_bindings;
    allocator.sets.reserve(max_sets);
    allocator.bindings.reserve(max_bindings);
}

void mdAddDescriptorBinding(u32 count, 
                            VkDescriptorType type, 
                            VkShaderStageFlagBits stage, 
                            const VkSampler *p_samplers,
                            MdDescriptorSetAllocator &allocator)
{
    VkDescriptorSetLayoutBinding binding = {};
    binding.descriptorCount = count;
    binding.descriptorType = type;
    binding.stageFlags = stage;
    binding.pImmutableSamplers = p_samplers;
    binding.binding = allocator.bindings.size();

    allocator.bindings.push_back(binding);
}

void mdUpdateDescriptorSetImage(MdRenderContext &context,
                                MdDescriptorSetAllocator &allocator, 
                                u32 binding_index,
                                u32 set_index,
                                MdGPUTexture &texture)
{
    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.sampler = texture.sampler;
    image_info.imageView = texture.image_view;
    
    VkWriteDescriptorSet write_set = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write_set.descriptorCount = 1;
    write_set.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_set.pBufferInfo = NULL;
    write_set.pImageInfo = &image_info;
    write_set.pTexelBufferView = NULL;
    write_set.dstBinding = binding_index;
    write_set.dstSet = allocator.sets[set_index];

    vkUpdateDescriptorSets(
        context.device, 
        1, 
        &write_set, 
        0, 
        NULL
    );
}

void mdUpdateDescriptorSetUBO(  MdRenderContext &context,
                                MdDescriptorSetAllocator &allocator, 
                                u32 binding_index,
                                u32 set_index,
                                MdGPUBuffer &buffer, 
                                u32 offset, 
                                u32 range)
{
    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = buffer.buffer;
    buffer_info.offset = offset;
    buffer_info.range = range;

    VkWriteDescriptorSet write_set = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write_set.descriptorCount = 1;
    write_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write_set.pBufferInfo = &buffer_info;
    write_set.pImageInfo = NULL;
    write_set.pTexelBufferView = NULL;
    write_set.dstBinding = binding_index;
    write_set.dstSet = allocator.sets[set_index];

    vkUpdateDescriptorSets(
        context.device, 
        1, 
        &write_set, 
        0, 
        NULL
    );
}

VkResult mdCreateDescriptorSets(u32 set_count, MdRenderContext &context, MdDescriptorSetAllocator &allocator)
{
    VkResult result = VK_ERROR_UNKNOWN;
    if (allocator.layout == VK_NULL_HANDLE)
    {
        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = allocator.bindings.size();
        layout_info.pBindings = allocator.bindings.data();
        layout_info.flags = 0;
        
        result = vkCreateDescriptorSetLayout(context.device, &layout_info, NULL, &allocator.layout);
        VK_CHECK(result, "failed to create descriptor set layout");
    }

    if (allocator.pool == VK_NULL_HANDLE)
    {
        std::vector<VkDescriptorPoolSize> pool_sizes;
        pool_sizes.reserve(allocator.bindings.size());
        for (u32 i=0; i<allocator.bindings.size(); i++)
        {
            pool_sizes.push_back({
                allocator.bindings[i].descriptorType, 
                allocator.bindings[i].descriptorCount
            });
        }

        VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.poolSizeCount = pool_sizes.size();
        pool_info.pPoolSizes = pool_sizes.data();
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = allocator.max_sets;
        
        result = vkCreateDescriptorPool(context.device, &pool_info, NULL, &allocator.pool);
        pool_sizes.clear();
        VK_CHECK(result, "failed to create descriptor pool");
    }

    if (allocator.sets.size()+set_count > allocator.max_sets)
    {
        LOG_ERROR("set count (%ld + %d) must not be greater than the max sets (%d)", 
            allocator.sets.size(), 
            set_count, 
            allocator.max_sets
        );
        return VK_ERROR_UNKNOWN;
    }

    u32 end_index = allocator.sets.size()+set_count;
    for (u32 i=allocator.sets.size();i<end_index;i++)
        allocator.sets.push_back(VK_NULL_HANDLE);

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = allocator.pool;
    alloc_info.descriptorSetCount = set_count;
    alloc_info.pSetLayouts = &allocator.layout;
    result = vkAllocateDescriptorSets(context.device, &alloc_info, allocator.sets.data());
    VK_CHECK(result, "failed to allocate descriptor sets");
    return result;
}

void mdDestroyDescriptorSetAllocator(MdRenderContext &context, MdDescriptorSetAllocator &allocator)
{
    if (allocator.sets.size() > 0)
        vkFreeDescriptorSets(context.device, allocator.pool, allocator.sets.size(), allocator.sets.data());

    allocator.sets.clear();
    allocator.bindings.clear();

    if (allocator.pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(context.device, allocator.pool, NULL);

    if (allocator.layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(context.device, allocator.layout, NULL);
}

#pragma endregion

#pragma region [ Geometry Input State ]
struct MdPipelineGeometryInputState
{
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::vector<VkVertexInputBindingDescription> bindings;

    VkPipelineVertexInputStateCreateInfo vertex_info = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo assembly_info = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
};

enum MdVertexComponentType
{
    MD_F32 = 0,
    MD_U64,
    MD_U32,
    MD_U16,
    MD_U8,
    MD_VERTEX_COMPONENT_TYPE_COUNT
};

VkFormat mdGeometryInputCalculateVertexFormat(MdVertexComponentType type, u32 count)
{
    const VkFormat format_matrix[] = {
        VK_FORMAT_R32_SFLOAT,VK_FORMAT_R64_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R16_UINT,VK_FORMAT_R8_UINT,
        VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R64G64_UINT,VK_FORMAT_R32G32_UINT,VK_FORMAT_R16G16_UINT,VK_FORMAT_R8G8_UINT,
        VK_FORMAT_R32G32B32_SFLOAT,VK_FORMAT_R64G64B64_UINT,VK_FORMAT_R32G32B32_UINT,VK_FORMAT_R16G16B16_UINT,VK_FORMAT_R8G8B8_UINT,
        VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R64G64B64A64_UINT,VK_FORMAT_R32G32B32A32_UINT,VK_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R8G8B8A8_UINT
    };

    return format_matrix[count*MD_VERTEX_COMPONENT_TYPE_COUNT + type];
}

void mdInitGeometryInputState(MdPipelineGeometryInputState &stage)
{
    stage.vertex_info.flags = 0;
    stage.vertex_info.vertexAttributeDescriptionCount = 0;
    stage.vertex_info.pVertexAttributeDescriptions = NULL;
    stage.vertex_info.vertexBindingDescriptionCount = 0;
    stage.vertex_info.pVertexBindingDescriptions = NULL;
    
    stage.assembly_info.flags = 0;
    stage.assembly_info.primitiveRestartEnable = VK_FALSE;
    stage.assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

void mdGeometryInputAddVertexBinding(   MdPipelineGeometryInputState &stage, 
                                        VkVertexInputRate rate, 
                                        u32 stride)
{
    VkVertexInputBindingDescription binding = {};
    binding.binding = stage.bindings.size();
    binding.inputRate = rate;
    binding.stride = stride;

    stage.bindings.push_back(binding);
}

void mdGeometryInputAddAttribute(   MdPipelineGeometryInputState &stage, 
                                    u32 binding, 
                                    u32 location, 
                                    u32 count, 
                                    MdVertexComponentType type, 
                                    u32 offset)
{
    if (binding + 1 > stage.bindings.size())
    {
        LOG_ERROR("binding index (%d) must not exceed current number of bindings (%d)\n", binding, stage.bindings.size());
        return;
    }

    u32 index = 0;
    bool found = false;
    for (u32 i=0; i<stage.attributes.size(); i++)
    {
        if (!(stage.attributes[i].binding == binding && stage.attributes[i].location == location))
            continue;

        index = i;
        break;
    }

    if (!found)
    {
        index = stage.attributes.size();
        stage.attributes.push_back({});
    }

    stage.attributes[index].binding = binding;
    stage.attributes[index].location = location;
    stage.attributes[index].format = mdGeometryInputCalculateVertexFormat(type, count);
    stage.attributes[index].offset = offset;
}
 
void MdGeometryInputStageSetTopology(   MdPipelineGeometryInputState &stage, 
                                        VkPrimitiveTopology topology, 
                                        VkBool32 primitive_restart = VK_FALSE)
{
    stage.assembly_info.topology = topology;
    stage.assembly_info.primitiveRestartEnable = primitive_restart;
}

void mdBuildGeometryInputState(MdPipelineGeometryInputState &stage)
{
    stage.vertex_info.vertexAttributeDescriptionCount = stage.attributes.size();
    stage.vertex_info.pVertexAttributeDescriptions = stage.attributes.data();
    stage.vertex_info.vertexBindingDescriptionCount = stage.bindings.size();
    stage.vertex_info.pVertexBindingDescriptions = stage.bindings.data();
}
#pragma endregion

#pragma region [ Rasterization State ]
struct MdPipelineRasterizationState
{
    VkPipelineRasterizationStateCreateInfo raster_info = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
};

void mdInitRasterizationState(MdPipelineRasterizationState &stage)
{
    stage.raster_info.flags = 0;
    stage.raster_info.rasterizerDiscardEnable = VK_FALSE;
    stage.raster_info.depthBiasEnable = VK_FALSE;
    stage.raster_info.depthBiasConstantFactor = 1.0f;
    stage.raster_info.depthBiasSlopeFactor = 0.0f;
    stage.raster_info.depthClampEnable = VK_FALSE;
    stage.raster_info.depthBiasClamp = 0.0f;
    stage.raster_info.cullMode = VK_CULL_MODE_NONE;
    stage.raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    stage.raster_info.polygonMode = VK_POLYGON_MODE_FILL;
    stage.raster_info.lineWidth = 1.0f;
}

void mdRasterizationStateEnableDepthBias(   MdPipelineRasterizationState &stage,
                                            VkBool32 enable = VK_FALSE,
                                            f32 constant_factor = 1.0f,
                                            f32 slope_factor = 1.0f)
{
    stage.raster_info.depthBiasEnable = enable;
    stage.raster_info.depthBiasConstantFactor = constant_factor;
    stage.raster_info.depthBiasSlopeFactor = slope_factor;
}

void mdRasterizationStateEnableDepthClamp(  MdPipelineRasterizationState &stage,
                                            VkBool32 enable = VK_FALSE,
                                            f32 clamp = 1.0f)
{
    stage.raster_info.depthClampEnable = enable;
    stage.raster_info.depthBiasClamp = clamp;
}

void mdRasterizationStateSetCullMode(   MdPipelineRasterizationState &stage, 
                                        VkCullModeFlags mode, 
                                        VkFrontFace face = VK_FRONT_FACE_COUNTER_CLOCKWISE)
{
    stage.raster_info.cullMode = mode;
    stage.raster_info.frontFace = face;
}

void mdRasterizationStateSetPolygonMode(MdPipelineRasterizationState &stage,
                                        VkPolygonMode mode)
{
    stage.raster_info.polygonMode = mode;
}

void mdBuildRasterizationState(MdPipelineRasterizationState &stage){}
void mdBuildDefaultRasterizationState(MdPipelineRasterizationState &stage)
{
    mdInitRasterizationState(stage);
}
#pragma endregion

#pragma region [ Color Blend State ]
struct MdPipelineColorBlendState
{
    VkPipelineColorBlendStateCreateInfo color_blend_info = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    std::vector<VkPipelineColorBlendAttachmentState> attachment;
};

void mdInitColorBlendState(MdPipelineColorBlendState &stage)
{
    stage.color_blend_info.flags = 0;
    stage.color_blend_info.attachmentCount = 0;
    stage.color_blend_info.pAttachments = NULL;
    stage.color_blend_info.logicOpEnable = VK_FALSE;
    stage.color_blend_info.logicOp = VK_LOGIC_OP_COPY;
    
    stage.color_blend_info.blendConstants[0] = 0.0f;
    stage.color_blend_info.blendConstants[1] = 0.0f;
    stage.color_blend_info.blendConstants[2] = 0.0f;
    stage.color_blend_info.blendConstants[3] = 0.0f;
}

void mdColorBlendStateLogicOpEnable(MdPipelineColorBlendState &stage, 
                                    VkBool32 enable, 
                                    f32 c0 = 0.0f, 
                                    f32 c1 = 0.0f, 
                                    f32 c2 = 0.0f, 
                                    f32 c3 = 0.0f)
{
    stage.color_blend_info.logicOpEnable = enable;
    stage.color_blend_info.blendConstants[0] = c0;
    stage.color_blend_info.blendConstants[1] = c1;
    stage.color_blend_info.blendConstants[2] = c2;
    stage.color_blend_info.blendConstants[3] = c3;
}

void mdColorBlendStateAddAttachment(MdPipelineColorBlendState &stage, VkBool32 blend_enable = VK_FALSE)
{
    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.blendEnable = blend_enable;
    stage.attachment.push_back(attachment);
}

void mdColorBlendAttachmentBlendEnable(MdPipelineColorBlendState &stage, u32 index, VkBool32 enable = VK_FALSE)
{
    stage.attachment[index].blendEnable = enable;
}

void mdColorBlendAttachmentSetColorBlendOp( MdPipelineColorBlendState &stage, 
                                            u32 index, 
                                            VkBlendOp op,
                                            VkBlendFactor src_factor = VK_BLEND_FACTOR_ONE,
                                            VkBlendFactor dst_factor = VK_BLEND_FACTOR_ZERO)
{
    stage.attachment[index].colorBlendOp = op;
    stage.attachment[index].srcColorBlendFactor = src_factor;
    stage.attachment[index].dstColorBlendFactor = dst_factor;
}

void mdColorBlendAttachmentSetAlphaBlendOp( MdPipelineColorBlendState &stage, 
                                            u32 index, 
                                            VkBlendOp op,
                                            VkBlendFactor src_factor = VK_BLEND_FACTOR_ONE,
                                            VkBlendFactor dst_factor = VK_BLEND_FACTOR_ZERO)
{
    stage.attachment[index].alphaBlendOp = op;
    stage.attachment[index].srcAlphaBlendFactor = src_factor;
    stage.attachment[index].dstAlphaBlendFactor = dst_factor;
}

void mdColorBlendAttachmentSetColorWriteMask(   MdPipelineColorBlendState &stage, 
                                                u32 index, 
                                                VkColorComponentFlags mask)
{
    stage.attachment[index].colorWriteMask = mask;
}

void mdBuildColorBlendState(MdPipelineColorBlendState &stage)
{
    stage.color_blend_info.attachmentCount = stage.attachment.size();
    stage.color_blend_info.pAttachments = stage.attachment.data();
}

void mdBuildDefaultColorBlendState(MdPipelineColorBlendState &stage)
{
    mdInitColorBlendState(stage);
    stage.attachment.push_back({});
    stage.attachment[0].blendEnable = VK_FALSE;
    stage.attachment[0].colorBlendOp = VK_BLEND_OP_ADD;
    stage.attachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    stage.attachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    stage.attachment[0].alphaBlendOp = VK_BLEND_OP_ADD;
    stage.attachment[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    stage.attachment[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    stage.attachment[0].colorWriteMask = (
        VK_COLOR_COMPONENT_R_BIT | 
        VK_COLOR_COMPONENT_G_BIT | 
        VK_COLOR_COMPONENT_B_BIT | 
        VK_COLOR_COMPONENT_A_BIT
    );
    stage.color_blend_info.attachmentCount = 1;
    stage.color_blend_info.pAttachments = stage.attachment.data();
}
#pragma endregion

#pragma region [ Pipeline ]

struct MdPipelineState
{
    VkPipelineLayout layout;
    std::vector<VkPipeline> pipeline;
};

VkResult mdCreateGraphicsPipeline(  MdRenderContext &context, 
                                    MdShaderSource &shaders, 
                                    MdDescriptorSetAllocator *p_descriptor_sets,
                                    MdPipelineGeometryInputState *p_geometry_state,
                                    MdPipelineRasterizationState *p_raster_state,
                                    MdPipelineColorBlendState *p_color_blend_state,
                                    MdRenderTarget &target,
                                    u32 pipeline_count, 
                                    MdPipelineState &pipeline)
{
    VkResult result;

    VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.flags = 0;
    if (p_descriptor_sets == NULL)
    {
        layout_info.setLayoutCount = 0;
        layout_info.pSetLayouts = NULL;
    }
    else
    {
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &p_descriptor_sets->layout;
    }
    
    layout_info.pushConstantRangeCount = 0;
    layout_info.pPushConstantRanges = NULL;

    result = vkCreatePipelineLayout(context.device, &layout_info, NULL, &pipeline.layout);
    VK_CHECK(result, "failed to create pipeline layout");

    VkDynamicState states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamic_info = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    {
        dynamic_info.dynamicStateCount = sizeof(states)/sizeof(VkDynamicState);
        dynamic_info.pDynamicStates = states;
    }

    VkPipelineViewportStateCreateInfo viewport_info = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    {
        viewport_info.viewportCount = 1;
        viewport_info.scissorCount = 1;
        viewport_info.flags = 0;
    }

    VkPipelineMultisampleStateCreateInfo multisample_info = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    {
        multisample_info.alphaToCoverageEnable = VK_FALSE;
        multisample_info.alphaToOneEnable = VK_FALSE;
        multisample_info.sampleShadingEnable = VK_FALSE;
        multisample_info.pSampleMask = NULL;
        multisample_info.minSampleShading = 1.0f;
        multisample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisample_info.flags = 0;
    }

    VkPipelineDepthStencilStateCreateInfo depth_info = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_info.depthWriteEnable = VK_TRUE;
    depth_info.depthTestEnable = VK_TRUE;
    depth_info.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_info.depthBoundsTestEnable = VK_FALSE;
    depth_info.minDepthBounds = 0.0f;
    depth_info.maxDepthBounds = 1.0f;
    depth_info.stencilTestEnable = VK_FALSE;
    depth_info.front = {};
    depth_info.back = {};

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.flags = 0;
    pipeline_info.basePipelineIndex = -1;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.layout = pipeline.layout;
    pipeline_info.renderPass = target.pass;
    pipeline_info.subpass = 0;

    pipeline_info.stageCount = shaders.modules.size();
    pipeline_info.pStages = shaders.modules.data();

    pipeline_info.pDynamicState = &dynamic_info;
    pipeline_info.pViewportState = &viewport_info;
    pipeline_info.pVertexInputState = &p_geometry_state->vertex_info;
    pipeline_info.pInputAssemblyState = &p_geometry_state->assembly_info;
    pipeline_info.pRasterizationState = &p_raster_state->raster_info;
    pipeline_info.pColorBlendState = &p_color_blend_state->color_blend_info;
    pipeline_info.pDepthStencilState = &depth_info;
    pipeline_info.pMultisampleState = &multisample_info;
    pipeline_info.pTessellationState = NULL;

    pipeline.pipeline.reserve(pipeline_count);
    for (u32 i=0; i<pipeline_count; i++)
        pipeline.pipeline.push_back(VK_NULL_HANDLE);
    
    result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, pipeline.pipeline.data());
    VK_CHECK(result, "failed to create graphics pipeline");
    return result;
}

void mdDestroyPipelineState(MdRenderContext &context, MdPipelineState &pipeline)
{
    if (pipeline.pipeline.size() > 0)
    {
        for (auto p : pipeline.pipeline)
            if (p != VK_NULL_HANDLE)
                vkDestroyPipeline(context.device, p, NULL);
        
        if (pipeline.layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(context.device, pipeline.layout, NULL);
    }
}

#pragma endregion

#define EXIT(context, window) {\
    mdDestroyContext(context);\
    mdDestroyWindow(window);\
    return -1;\
}

int mdInitVulkan(MdRenderContext &context, MdWindow &window)
{
    // Create window
    MdResult result = mdCreateWindow(1920, 1080, "Midori Engine", window);
    MD_CHECK_ANY(result, -1, "failed to create window");
    
    // Init render context
    std::vector<const char*> instance_extensions;
    u32 count = 0;
    if (SDL_Vulkan_GetInstanceExtensions(window.window, &count, NULL) != SDL_TRUE)
    {
        LOG_ERROR("failed to get instance extensions: %s\n", SDL_GetError());
        return -1;
    }
    instance_extensions.reserve(count);
    SDL_Vulkan_GetInstanceExtensions(window.window, &count, instance_extensions.data());
    
    result = mdInitContext(context, instance_extensions);
    if (result != MD_SUCCESS) EXIT(context, window);
    
    mdGetWindowSurface(window, context.instance, &context.surface);
    if (context.surface == VK_NULL_HANDLE) EXIT(context, window);

    result = mdCreateDevice(context);
    if (result != MD_SUCCESS) EXIT(context, window);

    result = mdGetSwapchain(context);
    if (result != MD_SUCCESS) EXIT(context, window);

    return 0;
}

struct UBO
{
    Matrix4x4 u_model;
    Matrix4x4 u_view;
    Matrix4x4 u_projection;
    f32 u_resolution[2];
    f32 u_time;
};

struct MdCamera
{
    Matrix4x4 view, projection;
};

MdResult mdLoadOBJ(const char *p_filepath, float **pp_vertices, usize *p_size)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warning, error;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, p_filepath))
    {
        LOG_ERROR("failed to load obj file: %s %s\n", warning.c_str(), error.c_str());
        return MD_ERROR_OBJ_LOADING_FAILURE;
    }

    // Get vertex count (index count)
    u64 vtx_count = 0;
    for (usize si=0; si<shapes.size(); si++)
        for (usize i=0; i<shapes[si].mesh.num_face_vertices.size(); i++)
            vtx_count += shapes[si].mesh.num_face_vertices[i];
    
    printf("Vertex count: %d\n", vtx_count);

    float *verts = (float*)malloc(vtx_count*VERTEX_SIZE*sizeof(float));
    if (verts == NULL)
    {
        LOG_ERROR("failed to allocate memory for vertices");
        return MD_ERROR_MEMORY_ALLOCATION_FAILURE;
    }

    // Loop over all materials
    for (usize mi=0; mi<materials.size(); mi++)
        printf("Texture for material[%d]: %s\n", mi, materials[mi].diffuse_texname.c_str());

    // Loop over all vertices of all faces of all shapes in the mesh
    u64 vertex_index = 0;
    for (usize si=0; si<shapes.size(); si++)
    {
        // Index offset is incremented by the number of verts in a face
        usize index_offset = 0;
        for (usize f=0; f<shapes[si].mesh.num_face_vertices.size(); f++)
        {
            // Get number of verts for the given face
            size_t fv = shapes[si].mesh.num_face_vertices[f];
            bool estimate_normals = false;            

            // Load vertex data
            for (usize v=0; v<fv; v++)
            {
                // Get the list of indices for the given face
                tinyobj::index_t idx = shapes[si].mesh.indices[index_offset + v];
                
                // Vertex positions
                verts[(vertex_index+v)*VERTEX_SIZE + 0] = attrib.vertices[3*((usize)idx.vertex_index)+0];
                verts[(vertex_index+v)*VERTEX_SIZE + 1] = attrib.vertices[3*((usize)idx.vertex_index)+1];
                verts[(vertex_index+v)*VERTEX_SIZE + 2] = attrib.vertices[3*((usize)idx.vertex_index)+2];

                // Normals
                if (idx.normal_index >= 0)
                {
                    verts[(vertex_index+v)*VERTEX_SIZE + 3] = attrib.normals[3*((usize)idx.normal_index)+0];
                    verts[(vertex_index+v)*VERTEX_SIZE + 4] = attrib.normals[3*((usize)idx.normal_index)+1];
                    verts[(vertex_index+v)*VERTEX_SIZE + 5] = attrib.normals[3*((usize)idx.normal_index)+2];
                }
                else estimate_normals = true;

                // Texcoords
                if (idx.texcoord_index >= 0)
                {
                    verts[(vertex_index+v)*VERTEX_SIZE + 6] = attrib.texcoords[2*(usize)idx.texcoord_index+0];
                    verts[(vertex_index+v)*VERTEX_SIZE + 7] = attrib.texcoords[2*(usize)idx.texcoord_index+1];
                }
                else
                {
                    verts[(vertex_index+v)*VERTEX_SIZE + 6] = 0;
                    verts[(vertex_index+v)*VERTEX_SIZE + 7] = 0;
                }
            }

            // Calculate normals using cross product between face edges
            if (estimate_normals)
            {
                // Get edges
                Vector4 edge0(
                    verts[(vertex_index+1)*VERTEX_SIZE + 0]-verts[(vertex_index+0)*VERTEX_SIZE + 0],
                    verts[(vertex_index+1)*VERTEX_SIZE + 1]-verts[(vertex_index+0)*VERTEX_SIZE + 1],
                    verts[(vertex_index+1)*VERTEX_SIZE + 2]-verts[(vertex_index+0)*VERTEX_SIZE + 2],
                    0
                );
                Vector4 edge1(
                    verts[(vertex_index+2)*VERTEX_SIZE + 0]-verts[(vertex_index+0)*VERTEX_SIZE + 0],
                    verts[(vertex_index+2)*VERTEX_SIZE + 1]-verts[(vertex_index+0)*VERTEX_SIZE + 1],
                    verts[(vertex_index+2)*VERTEX_SIZE + 2]-verts[(vertex_index+0)*VERTEX_SIZE + 2],
                    0
                );
                Vector4 n = Vector4::Cross(edge0, edge1);
                
                for (usize v=0; v<fv; v++)
                {
                    verts[(vertex_index+v)*VERTEX_SIZE + 3] = n.xyzw[0];
                    verts[(vertex_index+v)*VERTEX_SIZE + 4] = n.xyzw[1];
                    verts[(vertex_index+v)*VERTEX_SIZE + 5] = n.xyzw[2];
                }
            }

            vertex_index += fv;
            index_offset += fv;
        }
    }

    *pp_vertices = verts;
    *p_size = vtx_count;

    return MD_SUCCESS;
}

int main()
{
    // Init SDL
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
    {
        LOG_ERROR("failed to init SDL: %s\n", SDL_GetError());
        return -1;
    }
    SDL_Vulkan_LoadLibrary(NULL);
    
    MdWindow window;
    MdRenderContext context;
    if (mdInitVulkan(context, window) != 0)
        return -1;

    // Get graphics queue
    MdRenderQueue graphics_queue;
    MdResult result = mdGetQueue(VK_QUEUE_GRAPHICS_BIT, context, graphics_queue);
    if (result != MD_SUCCESS) EXIT(context, window);

    // Create command pool and buffer
    MdCommandEncoder cmd_encoder = {};
    VkResult vk_result = mdCreateCommandEncoder(context, graphics_queue.queue_index, cmd_encoder, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);    
    if (vk_result != VK_SUCCESS)
    {
        LOG_ERROR("failed to create command pool");
        EXIT(context, window);
    }

    vk_result = mdAllocateCommandBuffers(context, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmd_encoder);
    if (vk_result != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate command buffers");
        EXIT(context, window);
    }

    // Allocator
    MdGPUAllocator gpu_allocator = {};
    mdCreateGPUAllocator(context, gpu_allocator, graphics_queue, 1024*1024*1024);
    
    // Image texture
    SDL_Surface *img_sdl;
    img_sdl = IMG_Load("../images/test.png");
    if (img_sdl == NULL)
    {
        LOG_ERROR("failed to load image: %s\n", SDL_GetError());
        EXIT(context, window);
    }
    u64 w = img_sdl->w, h = img_sdl->h;
    u64 size = img_sdl->pitch * h;

    printf("image_size: %d\n", size);

    u32 img_format = img_sdl->format->format;
    u32 desired_format = SDL_PIXELFORMAT_ABGR8888;

    MdGPUTexture texture = {};
    MdGPUTextureBuilder tex_builder = {};
    mdCreateTextureBuilder2D(tex_builder, w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, img_sdl->pitch);
    mdSetTextureUsage(tex_builder, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    mdSetFilterWrap(tex_builder, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    mdSetMipmapOptions(tex_builder, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    mdSetMagFilters(tex_builder, VK_FILTER_LINEAR, VK_FILTER_LINEAR);
    mdBuildTexture2D(context, tex_builder, gpu_allocator, texture, img_sdl->pixels);
    
    SDL_FreeSurface(img_sdl);

    // Depth texture
    MdGPUTexture depth_texture = {};
    MdGPUTextureBuilder depth_tex_builder = {};
    mdCreateTextureBuilder2D(depth_tex_builder, window.w, window.h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    mdSetTextureUsage(depth_tex_builder, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    vk_result = mdBuildDepthTexture2D(context, depth_tex_builder, gpu_allocator, depth_texture);
    VK_CHECK(vk_result, "failed to create depth texture");

    // Color texture
    MdGPUTexture color_texture = {};
    MdGPUTextureBuilder color_tex_builder = {};
    mdCreateTextureBuilder2D(color_tex_builder, window.w, window.h, context.swapchain.image_format, VK_IMAGE_ASPECT_COLOR_BIT, 4);
    mdSetTextureUsage(color_tex_builder, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    mdSetFilterWrap(color_tex_builder, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    mdSetMipmapOptions(color_tex_builder, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    mdSetMagFilters(color_tex_builder, VK_FILTER_LINEAR, VK_FILTER_LINEAR);
    mdBuildColorAttachmentTexture2D(context, color_tex_builder, gpu_allocator, color_texture);
    
    // Descriptors
    MdDescriptorSetAllocator desc_allocator = {};
    mdCreateDescriptorAllocator(1, 4, desc_allocator);
    mdAddDescriptorBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL, NULL, desc_allocator);
    mdAddDescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, &texture.sampler, desc_allocator);
    mdAddDescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, &color_texture.sampler, desc_allocator);
    vk_result = mdCreateDescriptorSets(1, context, desc_allocator);
    if (vk_result != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate descriptor sets");
        EXIT(context, window);
    }

    // Vertex buffer
    MdGPUBuffer vertex_buffer = {};
    MdGPUBuffer index_buffer = {};
    float *geometry;
    usize geometry_size;
    mdLoadOBJ("../models/teapot/teapot.obj", &geometry, &geometry_size);

    mdAllocateGPUBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, geometry_size*VERTEX_SIZE*sizeof(f32), gpu_allocator, vertex_buffer);
    mdUploadToGPUBuffer(context, gpu_allocator, 0, geometry_size*VERTEX_SIZE*sizeof(f32), geometry, vertex_buffer);
    free(geometry);

    Matrix4x4 model(
        1, 0, 0, 0,
        0, -1, 0, 2,
        0, 0, 1, -15,
        0, 0, 0, 1
    );

    // UBO
    MdGPUBuffer uniform_buffer = {};
    UBO ubo = {};
    {
        ubo.u_resolution[0] = window.w;
        ubo.u_resolution[1] = window.h;
        ubo.u_time = 0.0f;
        ubo.u_projection = Matrix4x4::Perspective(45., (float)window.w/(float)window.h, 0.1f, 1000.0f);
        ubo.u_view = Matrix4x4::LookAt(
            Vector4(0,0,-1,0), 
            Vector4(0,0,1,0), 
            Vector4(0,1,0,0)
        );
        ubo.u_model = model;

        mdAllocateGPUUniformBuffer(sizeof(ubo), gpu_allocator, uniform_buffer);
        mdUploadToUniformBuffer(context, gpu_allocator, 0, sizeof(ubo), &ubo, uniform_buffer);
    }

    // Render target A
    MdRenderTarget render_target_A = {};
    MdRenderTargetBuilder builder_A = {};
    {
        mdCreateRenderTargetBuilder(context, window.w, window.h, builder_A);
        mdRenderTargetAddColorAttachment(builder_A, context.swapchain.image_format);
        mdRenderTargetAddDepthAttachment(builder_A, depth_texture.format);

        vk_result = mdBuildRenderTarget(context, builder_A, render_target_A);
        VK_CHECK(vk_result, "failed to build render target");

        std::vector<VkImageView> attachments;
        attachments.push_back(color_texture.image_view);
        attachments.push_back(depth_texture.image_view);
        
        vk_result = mdRenderTargetAddFramebuffer(context, render_target_A, attachments);
        VK_CHECK(vk_result, "failed to add framebuffer");
    }

    // Pipeline A
    MdPipelineGeometryInputState geometry_state_A = {}; 
    MdPipelineRasterizationState raster_state_A = {};
    MdPipelineColorBlendState color_blend_state_A = {};
    MdPipelineState pipeline_A;
    {    
        // Shaders
        MdShaderSource source;
        vk_result = mdLoadShaderSPIRV(context, sizeof(test_vsh_spirv), (const u32*)test_vsh_spirv, VK_SHADER_STAGE_VERTEX_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load vertex shader");
            EXIT(context, window);
        }

        vk_result = mdLoadShaderSPIRV(context, sizeof(test_fsh_spirv), (const u32*)test_fsh_spirv, VK_SHADER_STAGE_FRAGMENT_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load fragment shader");
            EXIT(context, window);
        }

        mdInitGeometryInputState(geometry_state_A);
        mdGeometryInputAddVertexBinding(geometry_state_A, VK_VERTEX_INPUT_RATE_VERTEX, 8*sizeof(f32));
        mdGeometryInputAddAttribute(geometry_state_A, 0, 0, 3, MD_F32, 0);
        mdGeometryInputAddAttribute(geometry_state_A, 0, 1, 3, MD_F32, 3*sizeof(f32));
        mdGeometryInputAddAttribute(geometry_state_A, 0, 2, 2, MD_F32, 6*sizeof(f32));
        mdBuildGeometryInputState(geometry_state_A);

        mdBuildDefaultRasterizationState(raster_state_A);

        mdBuildDefaultColorBlendState(color_blend_state_A);

        vk_result = mdCreateGraphicsPipeline(
            context, 
            source, 
            &desc_allocator,
            &geometry_state_A,
            &raster_state_A,
            &color_blend_state_A,
            render_target_A,
            1, 
            pipeline_A
        );
        mdDestroyShaderSource(context, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to create graphics pipeline");
            EXIT(context, window);
        }
    }

    // Render target B
    MdRenderTarget render_target_B = {};
    MdRenderTargetBuilder builder_B = {};
    {
        mdCreateRenderTargetBuilder(context, window.w, window.h, builder_B);
        mdRenderTargetAddColorAttachment(builder_B, context.swapchain.image_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        
        vk_result = mdBuildRenderTarget(context, builder_B, render_target_B);
        VK_CHECK(vk_result, "failed to build render target");

        std::vector<VkImageView> attachments;
        attachments.push_back(VK_NULL_HANDLE);

        for (u32 i=0; i<context.sw_image_views.size(); i++)
        {
            attachments[0] = context.sw_image_views[i];
            vk_result = mdRenderTargetAddFramebuffer(context, render_target_B, attachments);
            VK_CHECK(vk_result, "failed to add framebuffer");
        }
    }

    // Pipeline B
    MdPipelineGeometryInputState geometry_state_B = {}; 
    MdPipelineRasterizationState raster_state_B = {};
    MdPipelineColorBlendState color_blend_state_B = {};
    MdPipelineState pipeline_B;
    {    
        // Shaders
        MdShaderSource source;
        vk_result = mdLoadShaderSPIRV(context, sizeof(test_vsh_2_spirv), (const u32*)test_vsh_2_spirv, VK_SHADER_STAGE_VERTEX_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load vertex shader");
            EXIT(context, window);
        }

        vk_result = mdLoadShaderSPIRV(context, sizeof(test_fsh_2_spirv), (const u32*)test_fsh_2_spirv, VK_SHADER_STAGE_FRAGMENT_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load fragment shader");
            EXIT(context, window);
        }

        mdInitGeometryInputState(geometry_state_B);
        mdBuildGeometryInputState(geometry_state_B);

        mdBuildDefaultRasterizationState(raster_state_B);

        mdBuildDefaultColorBlendState(color_blend_state_B);

        vk_result = mdCreateGraphicsPipeline(
            context, 
            source, 
            &desc_allocator,
            &geometry_state_B,
            &raster_state_B,
            &color_blend_state_B,
            render_target_B,
            1, 
            pipeline_B
        );
        mdDestroyShaderSource(context, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to create graphics pipeline");
            EXIT(context, window);
        }
    }

    // Fences
    VkSemaphore image_available, render_finished;
    VkFence in_flight;
    {
        VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    
        semaphore_info.flags = 0;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
        vkCreateSemaphore(context.device, &semaphore_info, NULL, &image_available);
        vkCreateSemaphore(context.device, &semaphore_info, NULL, &render_finished);
        vkCreateFence(context.device, &fence_info, NULL, &in_flight);
    }

    // Viewport
    VkViewport viewport = {};
    VkRect2D scissor = {};
    {
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = window.w;
        viewport.height = window.h;

        scissor.offset = {0,0};
        scissor.extent = {window.w, window.h};
    }
    // Main render loop
    bool quit = false;
    SDL_Event event;
    
    u32 image_index = 0;
    i32 max_frames = -1;
    u32 frame_count = 0;

    mdUpdateDescriptorSetImage(context, desc_allocator, 1, 0, texture);
    mdUpdateDescriptorSetImage(context, desc_allocator, 2, 0, color_texture);
    
    VkDeviceSize offsets[1] = {0};
    do 
    {
        // Handle events
        while(SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_QUIT: 
                    quit = true; 
                    break;
                case SDL_WINDOWEVENT:
                    switch (event.window.event)
                    {
                        case SDL_WINDOWEVENT_RESIZED:
                        case SDL_WINDOWEVENT_SIZE_CHANGED:
                            printf("resized to %d, %d", event.window.data1, event.window.data2);

                            if (window.event == MD_WINDOW_RESIZED)
                            {
                                vkDeviceWaitIdle(context.device);

                                int ow = window.w, oh = window.h, nw, nh;
                                SDL_GetWindowSize(window.window, &nw, &nh);
                                if (ow != nw || oh != nh)
                                    continue;
                                
                                //mdRebuildSwapchain(context, nw, nh);
                                window.event = MD_WINDOW_UNCHANGED;
                            }
                        break;
                    }
                    break;
            }
        }

        vkWaitForFences(context.device, 1, &in_flight, VK_TRUE, UINT64_MAX);
        
        if (max_frames > -1 && frame_count++ >= max_frames)
            break;

        vk_result = vkAcquireNextImageKHR(
            context.device, 
            context.swapchain, 
            UINT64_MAX, 
            image_available, 
            VK_NULL_HANDLE, 
            &image_index
        );
        
        if (vk_result != VK_SUCCESS)
        {
            // Rebuild swapchain
            if (vk_result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                window.event = MD_WINDOW_RESIZED;
                continue;
            }
            else break;
        }

        vkResetFences(context.device, 1, &in_flight);
        
        // Update descriptors
        {
            ubo.u_time = SDL_GetTicks() / 1000.0f;
            mdUploadToUniformBuffer(context, gpu_allocator, 0, sizeof(ubo), &ubo, uniform_buffer);
            mdUpdateDescriptorSetUBO(
                context, 
                desc_allocator, 
                0, 
                0, 
                uniform_buffer, 
                0, 
                sizeof(ubo)
            );
        }
        
        // Command recording
        {
            VkClearValue clear_values[2];
            clear_values[0] = {0, 1, 0, 1};
            clear_values[1].depthStencil = {1.0f, 0};

            VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin_info.pInheritanceInfo = NULL;
            begin_info.flags = 0;
            vkResetCommandBuffer(cmd_encoder.buffers[0], 0);
            vkBeginCommandBuffer(cmd_encoder.buffers[0], &begin_info);

            // Pass A
            {
                VkRenderPassBeginInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                rp_info.renderPass = render_target_A.pass;
                rp_info.renderArea.offset = {0,0};
                rp_info.renderArea.extent = {window.w, window.h};
                rp_info.clearValueCount = 2;
                rp_info.pClearValues = clear_values;
                rp_info.framebuffer = render_target_A.buffers[0];
                vkCmdBeginRenderPass(cmd_encoder.buffers[0], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdSetViewport(cmd_encoder.buffers[0], 0, 1, &viewport);
                vkCmdSetScissor(cmd_encoder.buffers[0], 0, 1, &scissor);

                vkCmdBindVertexBuffers(
                    cmd_encoder.buffers[0], 
                    0, 
                    1, 
                    &vertex_buffer.buffer, offsets
                );
                vkCmdBindDescriptorSets(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_A.layout, 
                    0, 
                    desc_allocator.sets.size(), 
                    desc_allocator.sets.data(), 
                    0, 
                    NULL
                );
                vkCmdBindPipeline(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_A.pipeline[0]
                );
                vkCmdDraw(cmd_encoder.buffers[0], geometry_size, 1, 0, 0);
                vkCmdEndRenderPass(cmd_encoder.buffers[0]);
            }
            // Pass B
            {
                mdTransitionImageLayout(
                    color_texture, 
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
                    VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    cmd_encoder.buffers[0]
                );
                VkRenderPassBeginInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                rp_info.renderPass = render_target_B.pass;
                rp_info.renderArea.offset = {0,0};
                rp_info.renderArea.extent = {window.w, window.h};
                rp_info.clearValueCount = 2;
                rp_info.pClearValues = clear_values;
                rp_info.framebuffer = render_target_B.buffers[image_index];
                vkCmdBeginRenderPass(cmd_encoder.buffers[0], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdSetViewport(cmd_encoder.buffers[0], 0, 1, &viewport);
                vkCmdSetScissor(cmd_encoder.buffers[0], 0, 1, &scissor);
                vkCmdBindDescriptorSets(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_B.layout, 
                    0, 
                    desc_allocator.sets.size(), 
                    desc_allocator.sets.data(), 
                    0, 
                    NULL
                );
                vkCmdBindPipeline(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_B.pipeline[0]
                );
                vkCmdDraw(cmd_encoder.buffers[0], 3, 1, 0, 0);
                vkCmdEndRenderPass(cmd_encoder.buffers[0]);
            }
            
            vkEndCommandBuffer(cmd_encoder.buffers[0]);
        }

        // Submit to queue and present image
        {
            VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd_encoder.buffers[0];
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &image_available;
            submit_info.pWaitDstStageMask = &wait_stages;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &render_finished;
            vk_result = vkQueueSubmit(graphics_queue.queue_handle, 1, &submit_info, in_flight);
            if (vk_result != VK_SUCCESS)
                break;
            
            VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &context.swapchain.swapchain;
            present_info.pImageIndices = &image_index;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &render_finished;
            vk_result = vkQueuePresentKHR(graphics_queue.queue_handle, &present_info); 
            if (vk_result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                window.event = MD_WINDOW_RESIZED;
            }
        }
    }
    while(!quit);

    vkQueueWaitIdle(graphics_queue.queue_handle);

    // Destroy fences and semaphores
    vkDestroySemaphore(context.device, image_available, NULL);
    vkDestroySemaphore(context.device, render_finished, NULL);
    vkDestroyFence(context.device, in_flight, NULL);

    // Destroy texture
    mdDestroyTexture(gpu_allocator, texture);

    // Destroy render pass
    mdDestroyRenderTarget(context, render_target_A);
    mdDestroyRenderTarget(context, render_target_B);
    mdDestroyTexture(gpu_allocator, color_texture);
    mdDestroyTexture(gpu_allocator, depth_texture);

    // Free GPU memory
    mdFreeGPUBuffer(gpu_allocator, uniform_buffer);
    mdFreeGPUBuffer(gpu_allocator, vertex_buffer);
    mdDestroyGPUAllocator(gpu_allocator);

    mdDestroyPipelineState(context, pipeline_A);
    mdDestroyPipelineState(context, pipeline_B);
    mdDestroyDescriptorSetAllocator(context, desc_allocator);
    mdDestroyCommandEncoder(context, cmd_encoder);

    // Destroy context and window
    mdDestroyContext(context);
    mdDestroyWindow(window);

    SDL_Quit();
    return 0;
}