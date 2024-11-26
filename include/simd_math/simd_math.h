#pragma once
#include "../typedefs.h"

#ifdef ARCH_AMD64_SSE
    #include <immintrin.h>
    #define VECTOR_TYPE __m128
#else
    #error "No architecture defined"
#endif

#define PI 3.14159265359

enum Vector4_Component{ X=0, Y=1, Z=2, W=3 };

struct Vector4
{
    union
    {
        float xyzw[4];
        VECTOR_TYPE xyzw_m128;
    };
    Vector4(f32 x, f32 y, f32 z, f32 w) : xyzw_m128(_mm_set_ps(w,z,y,x)) {  }
    Vector4(f32 v[4]) : xyzw_m128(_mm_load_ps(v)) {  }
    Vector4(const VECTOR_TYPE &v) : xyzw_m128(v) {}
    Vector4(const Vector4 &v) {this->xyzw_m128 = v.xyzw_m128;}
    Vector4() : xyzw_m128(_mm_set1_ps(0)) {}

    operator VECTOR_TYPE() const;
    Vector4 operator=(const VECTOR_TYPE &v);
    Vector4 operator=(const Vector4 &v);

    VECTOR_TYPE operator+(const VECTOR_TYPE &rhs);
    VECTOR_TYPE operator-(const VECTOR_TYPE &rhs);
    VECTOR_TYPE operator*(const VECTOR_TYPE &rhs);
    VECTOR_TYPE operator/(const VECTOR_TYPE &rhs);
    VECTOR_TYPE operator+=(const VECTOR_TYPE &rhs);
    VECTOR_TYPE operator-=(const VECTOR_TYPE &rhs);
    VECTOR_TYPE operator*=(const VECTOR_TYPE &rhs);
    VECTOR_TYPE operator/=(const VECTOR_TYPE &rhs);
    
    VECTOR_TYPE operator+(const f32 &rhs);
    VECTOR_TYPE operator-(const f32 &rhs);
    VECTOR_TYPE operator*(const f32 &rhs);
    VECTOR_TYPE operator/(const f32 &rhs);
    VECTOR_TYPE operator+=(const f32 &rhs);
    VECTOR_TYPE operator-=(const f32 &rhs);
    VECTOR_TYPE operator*=(const f32 &rhs);
    VECTOR_TYPE operator/=(const f32 &rhs);
    
    static f32 Dot(const VECTOR_TYPE &lhs, const VECTOR_TYPE &rhs);
    static VECTOR_TYPE Cross(const VECTOR_TYPE &a, const VECTOR_TYPE &b);

    f32 Length();
    f32 LengthSquared();
    VECTOR_TYPE Normalize();

    static f32 Length(const VECTOR_TYPE &v);
    static f32 LengthSquared(const VECTOR_TYPE &v);
    static VECTOR_TYPE Normalize(const VECTOR_TYPE &vin);

    static VECTOR_TYPE Lerp(const VECTOR_TYPE &a, const VECTOR_TYPE &b, const f32 t);

    static VECTOR_TYPE SmoothDamp(   const VECTOR_TYPE &from, 
                                const VECTOR_TYPE &to, 
                                VECTOR_TYPE &vel, 
                                f32 smoothTime, 
                                f32 deltaTime);

    f32 GetXYZW(Vector4_Component c);

    static void GetXYZW(const VECTOR_TYPE &v, f32 xyzw[4]);
    void GetXYZW(f32 v[4]);
};

struct Matrix4x4
{
    union
    {
        struct {VECTOR_TYPE c0, c1, c2, c3;};
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
    Matrix4x4(const VECTOR_TYPE &a, const VECTOR_TYPE &b, const VECTOR_TYPE &c, const VECTOR_TYPE &d) :
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

    Matrix4x4 operator+(const Matrix4x4 &m);
    Matrix4x4 operator-(const Matrix4x4 &m);
    Matrix4x4 operator+=(const Matrix4x4 &m);
    Matrix4x4 operator-=(const Matrix4x4 &m);
    Matrix4x4 operator*(const float s);
    Matrix4x4 operator/(const float s);

    Vector4 operator*(const VECTOR_TYPE &v);
    Matrix4x4 operator*(const Matrix4x4 &m);
    Matrix4x4 operator*=(const Matrix4x4 &m);

    Matrix4x4 Transpose();
    static Matrix4x4 Transpose(const Matrix4x4 &m);

    static void Store(float a[16], const Matrix4x4 &m);

    static Matrix4x4 Perspective(float fov, float ar, float near, float far);
    static Matrix4x4 Orthographic(float l, float r, float b, float t, float n, float f);
    static Matrix4x4 LookAt(Vector4 camera, Vector4 eye, Vector4 up);
};