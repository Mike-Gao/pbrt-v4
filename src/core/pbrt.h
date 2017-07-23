
/*
    pbrt source code is Copyright(c) 1998-2016
                        Matt Pharr, Greg Humphreys, and Wenzel Jakob.

    This file is part of pbrt.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */

#if defined(_MSC_VER)
#define NOMINMAX
#pragma once
#endif

#ifndef PBRT_CORE_PBRT_H
#define PBRT_CORE_PBRT_H

// core/pbrt.h*
#include <string>

// Platform-specific definitions
#if defined(_WIN32) || defined(_WIN64)
  #define PBRT_IS_WINDOWS
#endif

#if defined(_MSC_VER)
  #define PBRT_IS_MSVC
  #if _MSC_VER == 1800
    #define snprintf _snprintf
  #endif
#endif

#ifndef PBRT_L1_CACHE_LINE_SIZE
  #define PBRT_L1_CACHE_LINE_SIZE 64
#endif

#include <stdint.h>
#if defined(PBRT_IS_MSVC)
#include <float.h>
#include <intrin.h>
#pragma warning(disable : 4305)  // double constant assigned to float
#pragma warning(disable : 4244)  // int -> float conversion
#pragma warning(disable : 4843)  // double -> float conversion
#endif

namespace pbrt {

// Global Forward Declarations
class Scene;
class Integrator;
class SamplerIntegrator;
template <typename T>
class Vector2;
template <typename T>
class Vector3;
template <typename T>
class Point3;
template <typename T>
class Point2;
template <typename T>
class Normal3;
class Ray;
class RayDifferential;
template <typename T>
class Bounds2;
template <typename T>
class Bounds3;
class Transform;
class Interaction;
class SurfaceInteraction;
class Shape;
class Primitive;
class GeometricPrimitive;
template <typename Child, int nSpectrumSamples>
class CoefficientSpectrum;
class RGBSpectrum;
class SampledSpectrum;
using Spectrum = RGBSpectrum;
// using Spectrum = SampledSpectrum;
class Camera;
struct CameraSample;
class ProjectiveCamera;
class Sampler;
class Filter;
class Film;
class FilmTile;
class BxDF;
class BSDF;
class MicrofacetDistribution;
class Material;
template <typename T>
class Texture;
class Medium;
class MediumInteraction;
struct MediumInterface;
class BSSRDF;
class SeparableBSSRDF;
class TabulatedBSSRDF;
struct BSSRDFTable;
class Light;
class VisibilityTester;
class AreaLight;
class Distribution1D;
class Distribution2D;
//#define PBRT_FLOAT_AS_DOUBLE
#ifdef PBRT_FLOAT_AS_DOUBLE
using Float = double;
#else
using Float = float;
#endif  // PBRT_FLOAT_AS_DOUBLE
class RNG;
class ProgressReporter;
class MemoryArena;
class Matrix4x4;
class ParamSet;
struct Options {
    int nThreads = 0;
    int texCacheMB = 96;
    int texReadMinMS = 0;
    bool quickRender = false;
    bool quiet = false;
    bool cat = false, toPly = false;
    std::string imageFile;  // FIXME need string included here
};

extern Options PbrtOptions;
class TextureParams;

 // TransportMode Declarations
enum class TransportMode { Radiance, Importance };

}  // namespace pbrt

#endif  // PBRT_CORE_PBRT_H
