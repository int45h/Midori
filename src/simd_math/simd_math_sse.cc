#define ARCH_AMD64_SSE
#include "../../include/simd_math/simd_math.h"

#include <immintrin.h>
#include <math.h>

#define PI 3.14159265359

f32 hadd_sse3(const VECTOR_TYPE &v)
{
    VECTOR_TYPE shuf = _mm_movehdup_ps(v);
    VECTOR_TYPE sums = _mm_add_ps(v, shuf);
    shuf = _mm_movehl_ps(sums, sums);
    sums = _mm_add_ps(shuf, sums);
    return _mm_cvtss_f32(sums);
}

Vector4::operator VECTOR_TYPE() const {return this->xyzw_m128;}
Vector4 Vector4::operator=(const VECTOR_TYPE &v) {this->xyzw_m128 = v; return *this;}
Vector4 Vector4::operator=(const Vector4 &v) {*this = v; return *this;}

VECTOR_TYPE Vector4::operator+(const VECTOR_TYPE &rhs) { return _mm_add_ps(this->xyzw_m128, rhs); }
VECTOR_TYPE Vector4::operator-(const VECTOR_TYPE &rhs) { return _mm_sub_ps(this->xyzw_m128, rhs); }
VECTOR_TYPE Vector4::operator*(const VECTOR_TYPE &rhs) { return _mm_mul_ps(this->xyzw_m128, rhs); }
VECTOR_TYPE Vector4::operator/(const VECTOR_TYPE &rhs) { return _mm_div_ps(this->xyzw_m128, rhs); }
VECTOR_TYPE Vector4::operator+=(const VECTOR_TYPE &rhs) { this->xyzw_m128 = _mm_add_ps(this->xyzw_m128, rhs); return this->xyzw_m128; }
VECTOR_TYPE Vector4::operator-=(const VECTOR_TYPE &rhs) { this->xyzw_m128 = _mm_sub_ps(this->xyzw_m128, rhs); return this->xyzw_m128; }
VECTOR_TYPE Vector4::operator*=(const VECTOR_TYPE &rhs) { this->xyzw_m128 = _mm_mul_ps(this->xyzw_m128, rhs); return this->xyzw_m128; }
VECTOR_TYPE Vector4::operator/=(const VECTOR_TYPE &rhs) { this->xyzw_m128 = _mm_div_ps(this->xyzw_m128, rhs); return this->xyzw_m128; }
    
VECTOR_TYPE Vector4::operator+(const f32 &rhs) { return _mm_add_ps(this->xyzw_m128, _mm_set1_ps(rhs)); }
VECTOR_TYPE Vector4::operator-(const f32 &rhs) { return _mm_sub_ps(this->xyzw_m128, _mm_set1_ps(rhs)); }
VECTOR_TYPE Vector4::operator*(const f32 &rhs) { return _mm_mul_ps(this->xyzw_m128, _mm_set1_ps(rhs)); }
VECTOR_TYPE Vector4::operator/(const f32 &rhs) { return _mm_div_ps(this->xyzw_m128, _mm_set1_ps(rhs)); }
VECTOR_TYPE Vector4::operator+=(const f32 &rhs) { this->xyzw_m128 = _mm_add_ps(this->xyzw_m128, _mm_set1_ps(rhs)); return this->xyzw_m128; }
VECTOR_TYPE Vector4::operator-=(const f32 &rhs) { this->xyzw_m128 = _mm_sub_ps(this->xyzw_m128, _mm_set1_ps(rhs)); return this->xyzw_m128; }
VECTOR_TYPE Vector4::operator*=(const f32 &rhs) { this->xyzw_m128 = _mm_mul_ps(this->xyzw_m128, _mm_set1_ps(rhs)); return this->xyzw_m128; }
VECTOR_TYPE Vector4::operator/=(const f32 &rhs) { this->xyzw_m128 = _mm_div_ps(this->xyzw_m128, _mm_set1_ps(rhs)); return this->xyzw_m128; }

f32 Vector4::Length(){ return sqrt(Dot(this->xyzw_m128, this->xyzw_m128)); }
f32 Vector4::LengthSquared(){ return Dot(this->xyzw_m128, this->xyzw_m128); }
VECTOR_TYPE Vector4::Normalize() 
{
    VECTOR_TYPE v = _mm_mul_ps(this->xyzw_m128, this->xyzw_m128);
    VECTOR_TYPE shuf = _mm_movehdup_ps(v);
    VECTOR_TYPE sums = _mm_add_ps(v, shuf);
    shuf = _mm_movehl_ps(sums, sums);
    sums = _mm_add_ps(shuf, sums);
    
    sums = _mm_rsqrt_ss(sums);
    return _mm_mul_ps(
        _mm_shuffle_ps(sums,sums,_MM_SHUFFLE(0,0,0,0)),
        v
    );
}

f32 Vector4::GetXYZW(Vector4_Component c)
{
    f32 xyzw[4] = {};
    _mm_store_ps(xyzw, this->xyzw_m128);
    return xyzw[c];
}
void Vector4::GetXYZW(f32 v[4]) { _mm_store_ps(v, this->xyzw_m128); }

f32 Vector4::Dot(const VECTOR_TYPE &lhs, const VECTOR_TYPE &rhs) { return hadd_sse3(_mm_mul_ps(lhs, rhs)); }
VECTOR_TYPE Vector4::Cross(const VECTOR_TYPE &a, const VECTOR_TYPE &b)
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

f32 Vector4::Length(const VECTOR_TYPE &v){ return sqrt(Dot(v,v)); }
f32 Vector4::LengthSquared(const VECTOR_TYPE &v){ return Dot(v,v); }
VECTOR_TYPE Vector4::Normalize(const VECTOR_TYPE &vin) 
{
    VECTOR_TYPE v = _mm_mul_ps(vin, vin);
    VECTOR_TYPE shuf = _mm_movehdup_ps(v);
    VECTOR_TYPE sums = _mm_add_ps(v, shuf);
    shuf = _mm_movehl_ps(sums, sums);
    sums = _mm_add_ps(shuf, sums);
    
    sums = _mm_rsqrt_ss(sums);
    return _mm_mul_ps(
        _mm_shuffle_ps(sums,sums,_MM_SHUFFLE(0,0,0,0)),
        v
    );
}

VECTOR_TYPE Vector4::Lerp(const VECTOR_TYPE &a, const VECTOR_TYPE &b, const f32 t)
{
    return _mm_add_ps(a,_mm_mul_ps(_mm_set1_ps(t), _mm_sub_ps(b,a)));
}

VECTOR_TYPE Vector4::SmoothDamp(  const VECTOR_TYPE &from, 
                    const VECTOR_TYPE &to, 
                    VECTOR_TYPE &vel, 
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
void Vector4::GetXYZW(const VECTOR_TYPE &v, f32 xyzw[4]) { _mm_store_ps(xyzw, v); }
    

Matrix4x4 Matrix4x4::operator+(const Matrix4x4 &m){ return Matrix4x4(_mm_add_ps(m.c0, this->c0),_mm_add_ps(m.c1, this->c1),_mm_add_ps(m.c2, this->c2),_mm_add_ps(m.c3, this->c3)); }
Matrix4x4 Matrix4x4::operator-(const Matrix4x4 &m){ return Matrix4x4(_mm_sub_ps(m.c0, this->c0),_mm_sub_ps(m.c1, this->c1),_mm_sub_ps(m.c2, this->c2),_mm_sub_ps(m.c3, this->c3)); }
Matrix4x4 Matrix4x4::operator+=(const Matrix4x4 &m){ *this = Matrix4x4(_mm_add_ps(m.c0, this->c0),_mm_add_ps(m.c1, this->c1),_mm_add_ps(m.c2, this->c2),_mm_add_ps(m.c3, this->c3)); return *this; }
Matrix4x4 Matrix4x4::operator-=(const Matrix4x4 &m){ *this = Matrix4x4(_mm_sub_ps(m.c0, this->c0),_mm_sub_ps(m.c1, this->c1),_mm_sub_ps(m.c2, this->c2),_mm_sub_ps(m.c3, this->c3)); return *this; }
Matrix4x4 Matrix4x4::operator*(const float s) { VECTOR_TYPE v = _mm_set1_ps(s); return Matrix4x4(_mm_mul_ps(v, this->c0),_mm_mul_ps(v, this->c1),_mm_mul_ps(v, this->c2),_mm_mul_ps(v, this->c3)); }
Matrix4x4 Matrix4x4::operator/(const float s) { VECTOR_TYPE v = _mm_set1_ps(1.0f/s); return Matrix4x4(_mm_mul_ps(v, this->c0),_mm_mul_ps(v, this->c1),_mm_mul_ps(v, this->c2),_mm_mul_ps(v, this->c3)); }

Vector4 Matrix4x4::operator*(const VECTOR_TYPE &v) 
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

Matrix4x4 Matrix4x4::operator*(const Matrix4x4 &m)
{
    VECTOR_TYPE c0 = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(0,0,0,0))),
            _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(1,1,1,1)))
        ),
        _mm_add_ps(
            _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(2,2,2,2))),
            _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(3,3,3,3)))
        )
    ); 
    VECTOR_TYPE c1 = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(0,0,0,0))),
            _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(1,1,1,1)))
        ),
        _mm_add_ps(
            _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(2,2,2,2))),
            _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(3,3,3,3)))
        )
    );
    VECTOR_TYPE c2 = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(0,0,0,0))),
            _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(1,1,1,1)))
        ),
        _mm_add_ps(
            _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(2,2,2,2))),
            _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(3,3,3,3)))
        )
    );
    VECTOR_TYPE c3 = _mm_add_ps(
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

Matrix4x4 Matrix4x4::operator*=(const Matrix4x4 &m)
{
    VECTOR_TYPE c0 = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(0,0,0,0))),
            _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(1,1,1,1)))
        ),
        _mm_add_ps(
            _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(2,2,2,2))),
            _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c0,m.c0,_MM_SHUFFLE(3,3,3,3)))
        )
    ); 
    VECTOR_TYPE c1 = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(0,0,0,0))),
            _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(1,1,1,1)))
        ),
        _mm_add_ps(
            _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(2,2,2,2))),
            _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c1,m.c1,_MM_SHUFFLE(3,3,3,3)))
        )
    );
    VECTOR_TYPE c2 = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps(this->c0, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(0,0,0,0))),
            _mm_mul_ps(this->c1, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(1,1,1,1)))
        ),
        _mm_add_ps(
            _mm_mul_ps(this->c2, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(2,2,2,2))),
            _mm_mul_ps(this->c3, _mm_shuffle_ps(m.c2,m.c2,_MM_SHUFFLE(3,3,3,3)))
        )
    );
    VECTOR_TYPE c3 = _mm_add_ps(
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

Matrix4x4 Matrix4x4::Transpose()
{
    VECTOR_TYPE A = _mm_unpacklo_ps(this->c0,this->c1);
    VECTOR_TYPE B = _mm_unpackhi_ps(this->c0,this->c1);
    VECTOR_TYPE C = _mm_unpacklo_ps(this->c2,this->c3);
    VECTOR_TYPE D = _mm_unpackhi_ps(this->c2,this->c3);
    
    return Matrix4x4(
        _mm_movelh_ps(A,C),
        _mm_movehl_ps(C,A),
        _mm_movelh_ps(B,D),
        _mm_movehl_ps(D,B)
    );
}

Matrix4x4 Matrix4x4::Transpose(const Matrix4x4 &m)
{
    VECTOR_TYPE A = _mm_unpacklo_ps(m.c0,m.c1);
    VECTOR_TYPE B = _mm_unpackhi_ps(m.c0,m.c1);
    VECTOR_TYPE C = _mm_unpacklo_ps(m.c2,m.c3);
    VECTOR_TYPE D = _mm_unpackhi_ps(m.c2,m.c3);
    
    return Matrix4x4(
        _mm_movelh_ps(A,C),
        _mm_movehl_ps(C,A),
        _mm_movelh_ps(B,D),
        _mm_movehl_ps(D,B)
    );
}

void Matrix4x4::Store(float a[16], const Matrix4x4 &m)
{
    _mm_store_ps((a+0),  m.c0);
    _mm_store_ps((a+4),  m.c1);
    _mm_store_ps((a+8),  m.c2);
    _mm_store_ps((a+12), m.c3);
}

Matrix4x4 Matrix4x4::Perspective(float fov, float ar, float near, float far)
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

Matrix4x4 Matrix4x4::Orthographic(float l, float r, float b, float t, float n, float f)
{
    float rl = 1 / (r-l);
    float bt = 1 / (b-t);
    float nf = 1 / (n-f);
    return Matrix4x4::Transpose(Matrix4x4(
        2*rl,0,0,0,
        0,2*bt,0,0,
        0,0,nf,0,
        -(r+l)*rl,-(b+t)*bt,n*nf,1
    ));
}

Matrix4x4 Matrix4x4::LookAt(Vector4 camera, Vector4 eye, Vector4 up)
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
