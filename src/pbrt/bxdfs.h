// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef PBRT_BXDFS_H
#define PBRT_BXDFS_H

#include <pbrt/pbrt.h>

#include <pbrt/base/bxdf.h>
#include <pbrt/interaction.h>
#include <pbrt/media.h>
#include <pbrt/options.h>
#include <pbrt/util/math.h>
#include <pbrt/util/memory.h>
#include <pbrt/util/pstd.h>
#include <pbrt/util/scattering.h>
#include <pbrt/util/spectrum.h>
#include <pbrt/util/taggedptr.h>
#include <pbrt/util/vecmath.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace pbrt {

// IdealDiffuseBxDF Definition
class IdealDiffuseBxDF {
  public:
    // IdealDiffuseBxDF Public Methods
    IdealDiffuseBxDF() = default;
    PBRT_CPU_GPU
    IdealDiffuseBxDF(const SampledSpectrum &R) : R(R) {}

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        if (!SameHemisphere(wo, wi))
            return SampledSpectrum(0.f);
        return R * InvPi;
    }

    PBRT_CPU_GPU
    BSDFSample Sample_f(Vector3f wo, Float uc, const Point2f &u, TransportMode mode,
                        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return {};
        Vector3f wi = SampleCosineHemisphere(u);
        if (wo.z < 0)
            wi.z *= -1;
        Float pdf = AbsCosTheta(wi) * InvPi;
        return BSDFSample(f(wo, wi, mode), wi, pdf, BxDFFlags::DiffuseReflection);
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return 0;
        if (SameHemisphere(wo, wi))
            return AbsCosTheta(wi) * InvPi;
        else
            return 0;
    }

    PBRT_CPU_GPU
    bool SampledPDFIsProportional() const { return false; }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "IdealDiffuseBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return R ? BxDFFlags::DiffuseReflection : BxDFFlags::Unset;
    }

  private:
    friend class SOA<IdealDiffuseBxDF>;
    SampledSpectrum R;
};

// DiffuseBxDF Definition
class DiffuseBxDF {
  public:
    // DiffuseBxDF Public Methods
    DiffuseBxDF() = default;
    PBRT_CPU_GPU
    DiffuseBxDF(const SampledSpectrum &R, const SampledSpectrum &T, Float sigma)
        : R(R), T(T) {
        Float sigma2 = Sqr(Radians(sigma));
        A = 1 - sigma2 / (2 * (sigma2 + 0.33f));
        B = 0.45f * sigma2 / (sigma2 + 0.09f);
    }

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        if (B == 0)
            return SameHemisphere(wo, wi) ? (R * InvPi) : (T * InvPi);

        if ((SameHemisphere(wo, wi) && !R) || (!SameHemisphere(wo, wi) && !T))
            return SampledSpectrum(0.);

        Float sinTheta_i = SinTheta(wi), sinTheta_o = SinTheta(wo);
        // Compute cosine term of Oren--Nayar model
        Float maxCos = 0;
        if (sinTheta_i > 0 && sinTheta_o > 0)
            maxCos = std::max<Float>(0, CosDPhi(wi, wo));

        // Compute sine and tangent terms of Oren--Nayar model
        Float sinAlpha, tanBeta;
        if (AbsCosTheta(wi) > AbsCosTheta(wo)) {
            sinAlpha = sinTheta_o;
            tanBeta = sinTheta_i / AbsCosTheta(wi);
        } else {
            sinAlpha = sinTheta_i;
            tanBeta = sinTheta_o / AbsCosTheta(wo);
        }

        if (SameHemisphere(wo, wi))
            return R * InvPi * (A + B * maxCos * sinAlpha * tanBeta);
        else
            return T * InvPi * (A + B * maxCos * sinAlpha * tanBeta);
    }

    PBRT_CPU_GPU
    BSDFSample Sample_f(Vector3f wo, Float uc, const Point2f &u, TransportMode mode,
                        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        Float pr = R.MaxComponentValue(), pt = T.MaxComponentValue();
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            pr = 0;
        if (!(sampleFlags & BxDFReflTransFlags::Transmission))
            pt = 0;
        if (pr == 0 && pt == 0)
            return {};

        Float cpdf;
        // TODO: rewrite to a single code path for the GPU. Good chance to
        // discuss divergence.
        if (SampleDiscrete({pr, pt}, uc, &cpdf) == 0) {
            Vector3f wi = SampleCosineHemisphere(u);
            if (wo.z < 0)
                wi.z *= -1;
            Float pdf = AbsCosTheta(wi) * InvPi * cpdf;
            return BSDFSample(f(wo, wi, mode), wi, pdf, BxDFFlags::DiffuseReflection);
        } else {
            Vector3f wi = SampleCosineHemisphere(u);
            if (wo.z > 0)
                wi.z *= -1;
            Float pdf = AbsCosTheta(wi) * InvPi * cpdf;
            return BSDFSample(f(wo, wi, mode), wi, pdf, BxDFFlags::DiffuseTransmission);
        }
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        Float pr = R.MaxComponentValue(), pt = T.MaxComponentValue();
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            pr = 0;
        if (!(sampleFlags & BxDFReflTransFlags::Transmission))
            pt = 0;
        if (pr == 0 && pt == 0)
            return 0;

        if (SameHemisphere(wo, wi))
            return pr / (pr + pt) * AbsCosTheta(wi) * InvPi;
        else
            return pt / (pr + pt) * AbsCosTheta(wi) * InvPi;
    }

    PBRT_CPU_GPU
    bool SampledPDFIsProportional() const { return false; }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "DiffuseBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return ((R ? BxDFFlags::DiffuseReflection : BxDFFlags::Unset) |
                (T ? BxDFFlags::DiffuseTransmission : BxDFFlags::Unset));
    }

  private:
    friend class SOA<DiffuseBxDF>;
    // DiffuseBxDF Private Members
    SampledSpectrum R, T;
    Float A, B;
};

// DielectricInterfaceBxDF Definition
class DielectricInterfaceBxDF {
  public:
    // DielectricInterfaceBxDF Public Methods
    DielectricInterfaceBxDF() = default;
    PBRT_CPU_GPU
    DielectricInterfaceBxDF(Float eta, const TrowbridgeReitzDistribution &mfDistrib)
        : eta(eta == 1 ? 1.001 : eta), mfDistrib(mfDistrib) {}

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return BxDFFlags(BxDFFlags::Reflection | BxDFFlags::Transmission |
                         BxDFFlags(mfDistrib.EffectivelySpecular() ? BxDFFlags::Specular
                                                                   : BxDFFlags::Glossy));
    }

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        if (mfDistrib.EffectivelySpecular())
            return SampledSpectrum(0);
        if (SameHemisphere(wo, wi)) {
            // Compute reflection at non-delta dielectric interface
            Float cosTheta_o = AbsCosTheta(wo), cosTheta_i = AbsCosTheta(wi);
            Vector3f wh = wi + wo;
            // Handle degenerate cases for microfacet reflection
            if (cosTheta_i == 0 || cosTheta_o == 0)
                return SampledSpectrum(0.);
            if (wh.x == 0 && wh.y == 0 && wh.z == 0)
                return SampledSpectrum(0.);
            wh = Normalize(wh);
            Float F = FrDielectric(Dot(wi, FaceForward(wh, Vector3f(0, 0, 1))), eta);
            return SampledSpectrum(mfDistrib.D(wh) * mfDistrib.G(wo, wi) * F /
                                   (4 * cosTheta_i * cosTheta_o));

        } else {
            // Compute transmission at non-delta dielectric interface
            Float cosTheta_o = CosTheta(wo), cosTheta_i = CosTheta(wi);
            if (cosTheta_i == 0 || cosTheta_o == 0)
                return {};
            // Compute $\wh$ from $\wo$ and $\wi$ for microfacet transmission
            Float etap = CosTheta(wo) > 0 ? eta : (1 / eta);
            Vector3f wh = wo + wi * etap;
            CHECK_RARE(1e-6, LengthSquared(wh) == 0);
            if (LengthSquared(wh) == 0)
                return {};
            wh = FaceForward(Normalize(wh), Normal3f(0, 0, 1));

            // both on same side?
            if (Dot(wi, wh) * Dot(wo, wh) > 0)
                return {};

            Float F = FrDielectric(Dot(wo, wh), eta);
            Float sqrtDenom = Dot(wo, wh) + etap * Dot(wi, wh);
            Float factor = (mode == TransportMode::Radiance) ? Sqr(1 / etap) : 1;
            return SampledSpectrum((1 - F) * factor *
                                   std::abs(mfDistrib.D(wh) * mfDistrib.G(wo, wi) *
                                            AbsDot(wi, wh) * AbsDot(wo, wh) /
                                            (cosTheta_i * cosTheta_o * Sqr(sqrtDenom))));
        }
    }

    PBRT_CPU_GPU
    BSDFSample Sample_f(Vector3f wo, Float uc, const Point2f &u, TransportMode mode,
                        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        if (wo.z == 0)
            return {};

        if (mfDistrib.EffectivelySpecular()) {
            // Sample delta dielectric interface
            Float R = FrDielectric(CosTheta(wo), eta), T = 1 - R;
            // Compute probabilities _pr_ and _pt_ for sampling reflection and
            // transmission
            Float pr = R, pt = T;
            if (!(sampleFlags & BxDFReflTransFlags::Reflection))
                pr = 0;
            if (!(sampleFlags & BxDFReflTransFlags::Transmission))
                pt = 0;
            if (pr == 0 && pt == 0)
                return {};

            if (uc < pr / (pr + pt)) {
                // Sample perfect specular reflection at interface
                Vector3f wi(-wo.x, -wo.y, wo.z);
                SampledSpectrum fr(R / AbsCosTheta(wi));
                return BSDFSample(fr, wi, pr / (pr + pt), BxDFFlags::SpecularReflection);

            } else {
                // Sample perfect specular transmission at interface
                // Figure out which $\eta$ is incident and which is transmitted
                bool entering = CosTheta(wo) > 0;
                Float etap = entering ? eta : (1 / eta);

                // Compute ray direction for specular transmission
                Vector3f wi;
                bool tir = !Refract(wo, FaceForward(Normal3f(0, 0, 1), wo), etap, &wi);
                CHECK_RARE(1e-6, tir);
                if (tir)
                    return {};

                SampledSpectrum ft(T / AbsCosTheta(wi));
                // Account for non-symmetry with transmission to different medium
                if (mode == TransportMode::Radiance)
                    ft /= Sqr(etap);

                return BSDFSample(ft, wi, pt / (pr + pt),
                                  BxDFFlags::SpecularTransmission);
            }

        } else {
            // Sample non-delta dielectric interface
            // Sample half-angle vector for outgoing direction and compute Frensel factor
            Vector3f wh = mfDistrib.Sample_wm(wo, u);
            Float F = FrDielectric(
                Dot(Reflect(wo, wh), FaceForward(wh, Vector3f(0, 0, 1))), eta);
            Float R = F, T = 1 - R;

            // Compute probabilities _pr_ and _pt_ for sampling reflection and
            // transmission
            Float pr = R, pt = T;
            if (!(sampleFlags & BxDFReflTransFlags::Reflection))
                pr = 0;
            if (!(sampleFlags & BxDFReflTransFlags::Transmission))
                pt = 0;
            if (pr == 0 && pt == 0)
                return {};

            if (uc < pr / (pr + pt)) {
                // Sample reflection at non-delta dielectric interface
                Vector3f wi = Reflect(wo, wh);
                CHECK_RARE(1e-6, Dot(wo, wh) <= 0);
                if (!SameHemisphere(wo, wi) || Dot(wo, wh) <= 0)
                    return {};

                // Compute PDF of _wi_ for microfacet reflection
                Float pdf = mfDistrib.PDF(wo, wh) / (4 * Dot(wo, wh)) * pr / (pr + pt);
                CHECK(!std::isnan(pdf));

                // TODO: reuse fragments from f()
                Float cosTheta_o = AbsCosTheta(wo), cosTheta_i = AbsCosTheta(wi);
                // Handle degenerate cases for microfacet reflection
                if (cosTheta_i == 0 || cosTheta_o == 0)
                    return {};
                SampledSpectrum f(mfDistrib.D(wh) * mfDistrib.G(wo, wi) * F /
                                  (4 * cosTheta_i * cosTheta_o));
                if (mfDistrib.EffectivelySpecular())
                    return BSDFSample(f / pdf, wi, 1, BxDFFlags::SpecularReflection);
                else
                    return BSDFSample(f, wi, pdf, BxDFFlags::GlossyReflection);

            } else {
                // Sample transmission at non-delta dielectric interface
                // FIXME (make consistent): this etap is 1/etap as used in
                // specular...
                Float etap = CosTheta(wo) > 0 ? eta : (1 / eta);
                Vector3f wi;
                bool tir = !Refract(wo, (Normal3f)wh, etap, &wi);
                CHECK_RARE(1e-6, tir);
                if (SameHemisphere(wo, wi))
                    return {};
                if (tir || wi.z == 0)
                    return {};

                // Evaluate BSDF
                // TODO: share fragments with f(), PDF()...
                wh = FaceForward(wh, Normal3f(0, 0, 1));

                Float sqrtDenom = Dot(wo, wh) + etap * Dot(wi, wh);
                Float factor = (mode == TransportMode::Radiance) ? Sqr(1 / etap) : 1;

                SampledSpectrum f(
                    (1 - F) * factor *
                    std::abs(mfDistrib.D(wh) * mfDistrib.G(wo, wi) * AbsDot(wi, wh) *
                             AbsDot(wo, wh) /
                             (AbsCosTheta(wi) * AbsCosTheta(wo) * Sqr(sqrtDenom))));

                // Compute PDF
                Float dwh_dwi =
                    /*Sqr(etap) * */ AbsDot(wi, wh) /
                    Sqr(Dot(wo, wh) + etap * Dot(wi, wh));
                Float pdf = mfDistrib.PDF(wo, wh) * dwh_dwi * pt / (pr + pt);
                CHECK(!std::isnan(pdf));

                if (mfDistrib.EffectivelySpecular())
                    return BSDFSample(f / pdf, wi, 1, BxDFFlags::SpecularTransmission);
                else
                    return BSDFSample(f, wi, pdf, BxDFFlags::GlossyTransmission);
            }
        }
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        if (mfDistrib.EffectivelySpecular())
            return 0;
        // Return PDF for non-delta dielectric interface
        if (SameHemisphere(wo, wi)) {
            if (!(sampleFlags & BxDFReflTransFlags::Reflection))
                return 0;

            Vector3f wh = wo + wi;
            CHECK_RARE(1e-6, LengthSquared(wh) == 0);
            CHECK_RARE(1e-6, Dot(wo, wh) < 0);
            if (LengthSquared(wh) == 0 || Dot(wo, wh) <= 0)
                return 0;

            wh = Normalize(wh);

            Float F = FrDielectric(Dot(wi, FaceForward(wh, Vector3f(0, 0, 1))), eta);
            CHECK_RARE(1e-6, F == 0);
            Float pr = F, pt = 1 - F;
            if (!(sampleFlags & BxDFReflTransFlags::Transmission))
                pt = 0;

            return mfDistrib.PDF(wo, wh) / (4 * Dot(wo, wh)) * pr / (pr + pt);
        } else {
            if (!(sampleFlags & BxDFReflTransFlags::Transmission))
                return 0;
            // Compute $\wh$ from $\wo$ and $\wi$ for microfacet transmission
            Float etap = CosTheta(wo) > 0 ? eta : (1 / eta);
            Vector3f wh = wo + wi * etap;
            CHECK_RARE(1e-6, LengthSquared(wh) == 0);
            if (LengthSquared(wh) == 0)
                return 0;
            wh = Normalize(wh);

            // both on same side?
            if (Dot(wi, wh) * Dot(wo, wh) > 0)
                return 0.;

            Float F = FrDielectric(Dot(wo, FaceForward(wh, Normal3f(0, 0, 1))), eta);
            Float pr = F, pt = 1 - F;
            if (pt == 0)
                return 0;
            if (!(sampleFlags & BxDFReflTransFlags::Reflection))
                pr = 0;

            // Compute change of variables _dwh\_dwi_ for microfacet
            // transmission
            Float dwh_dwi =
                /*Sqr(etap) * */ AbsDot(wi, wh) / Sqr(Dot(wo, wh) + etap * Dot(wi, wh));
            CHECK_RARE(1e-6, (1 - F) == 0);
            return mfDistrib.PDF(wo, wh) * dwh_dwi * pt / (pr + pt);
        }
    }

    PBRT_CPU_GPU
    bool SampledPDFIsProportional() const { return false; }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "DielectricInterfaceBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() { mfDistrib.Regularize(); }

  private:
    friend class SOA<DielectricInterfaceBxDF>;
    // DielectricInterfaceBxDF Private Members
    Float eta;
    TrowbridgeReitzDistribution mfDistrib;
};

// ThinDielectricBxDF Definition
class ThinDielectricBxDF {
  public:
    // ThinDielectric Public Methods
    ThinDielectricBxDF() = default;
    PBRT_CPU_GPU
    ThinDielectricBxDF(Float eta) : eta(eta) {}

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        return SampledSpectrum(0);
    }

    PBRT_CPU_GPU
    BSDFSample Sample_f(Vector3f wo, Float uc, const Point2f &u, TransportMode mode,
                        BxDFReflTransFlags sampleFlags) const {
        Float R = FrDielectric(CosTheta(wo), eta), T = 1 - R;
        // Compute _R_ and _T_ accounting for scattering between interfaces
        if (R < 1) {
            R += T * T * R / (1 - R * R);
            T = 1 - R;
        }

        // Compute probabilities _pr_ and _pt_ for sampling reflection and transmission
        Float pr = R, pt = T;
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            pr = 0;
        if (!(sampleFlags & BxDFReflTransFlags::Transmission))
            pt = 0;
        if (pr == 0 && pt == 0)
            return {};

        if (uc < pr / (pr + pt)) {
            // Sample perfect specular reflection at interface
            Vector3f wi(-wo.x, -wo.y, wo.z);
            SampledSpectrum fr(R / AbsCosTheta(wi));
            return BSDFSample(fr, wi, pr / (pr + pt), BxDFFlags::SpecularReflection);

        } else {
            // Sample perfect specular transmission at thin dielectric interface
            Vector3f wi = -wo;
            SampledSpectrum ft(T / AbsCosTheta(wi));
            return BSDFSample(ft, wi, pt / (pr + pt), BxDFFlags::SpecularTransmission);
        }
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const {
        return 0;
    }

    PBRT_CPU_GPU
    bool SampledPDFIsProportional() const { return false; }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "ThinDielectricBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() { /* TODO */
    }

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return (BxDFFlags::Reflection | BxDFFlags::Transmission | BxDFFlags::Specular);
    }

  private:
    friend class SOA<ThinDielectricBxDF>;
    Float eta;
};

// ConductorBxDF Definition
class ConductorBxDF {
  public:
    // ConductorBxDF Public Methods
    ConductorBxDF() = default;
    PBRT_CPU_GPU
    ConductorBxDF(const TrowbridgeReitzDistribution &mfDistrib,
                  const SampledSpectrum &eta, const SampledSpectrum &k)
        : mfDistrib(mfDistrib), eta(eta), k(k) {}

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        if (mfDistrib.EffectivelySpecular())
            return (BxDFFlags::Reflection | BxDFFlags::Specular);
        else
            return (BxDFFlags::Reflection | BxDFFlags::Glossy);
    }

    PBRT_CPU_GPU
    bool SampledPDFIsProportional() const { return false; }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "ConductorBxDF"; }
    std::string ToString() const;

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        if (!SameHemisphere(wo, wi))
            return {};
        if (mfDistrib.EffectivelySpecular())
            return {};
        Float cosTheta_o = AbsCosTheta(wo), cosTheta_i = AbsCosTheta(wi);
        Vector3f wh = wi + wo;
        // Handle degenerate cases for microfacet reflection
        if (cosTheta_i == 0 || cosTheta_o == 0)
            return {};
        if (wh.x == 0 && wh.y == 0 && wh.z == 0)
            return {};

        wh = Normalize(wh);
        Float frCosTheta_i = AbsDot(wi, FaceForward(wh, Vector3f(0, 0, 1)));
        SampledSpectrum F = FrConductor(frCosTheta_i, eta, k);
        return mfDistrib.D(wh) * mfDistrib.G(wo, wi) * F / (4 * cosTheta_i * cosTheta_o);
    }

    PBRT_CPU_GPU
    BSDFSample Sample_f(Vector3f wo, Float uc, const Point2f &u, TransportMode mode,
                        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return {};
        if (mfDistrib.EffectivelySpecular()) {
            // Compute perfect specular reflection direction
            Vector3f wi(-wo.x, -wo.y, wo.z);

            SampledSpectrum f = FrConductor(AbsCosTheta(wi), eta, k) / AbsCosTheta(wi);
            return BSDFSample(f, wi, 1, BxDFFlags::SpecularReflection);
        }

        // Sample microfacet orientation $\wh$ and reflected direction $\wi$
        if (wo.z == 0)
            return {};
        Vector3f wh = mfDistrib.Sample_wm(wo, u);
        Vector3f wi = Reflect(wo, wh);
        CHECK_RARE(1e-6, Dot(wo, wh) <= 0);
        if (!SameHemisphere(wo, wi) || Dot(wo, wh) <= 0)
            return {};

        // Compute PDF of _wi_ for microfacet reflection
        Float pdf = mfDistrib.PDF(wo, wh) / (4 * Dot(wo, wh));

        // TODO: reuse fragments from f()
        Float cosTheta_o = AbsCosTheta(wo), cosTheta_i = AbsCosTheta(wi);
        // Handle degenerate cases for microfacet reflection
        if (cosTheta_i == 0 || cosTheta_o == 0)
            return {};
        Float frCosTheta_i = AbsDot(wi, FaceForward(wh, Vector3f(0, 0, 1)));
        SampledSpectrum F = FrConductor(frCosTheta_i, eta, k);
        SampledSpectrum f =
            mfDistrib.D(wh) * mfDistrib.G(wo, wi) * F / (4 * cosTheta_i * cosTheta_o);
        return BSDFSample(f, wi, pdf, BxDFFlags::GlossyReflection);
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return 0;
        if (!SameHemisphere(wo, wi))
            return 0;
        if (mfDistrib.EffectivelySpecular())
            return 0;
        Vector3f wh = wo + wi;
        CHECK_RARE(1e-6, LengthSquared(wh) == 0);
        CHECK_RARE(1e-6, Dot(wo, wh) < 0);
        if (LengthSquared(wh) == 0 || Dot(wo, wh) <= 0)
            return 0;
        wh = Normalize(wh);
        return mfDistrib.PDF(wo, wh) / (4 * Dot(wo, wh));
    }

    PBRT_CPU_GPU
    void Regularize() { mfDistrib.Regularize(); }

  private:
    friend class SOA<ConductorBxDF>;
    // ConductorBxDF Private Members
    TrowbridgeReitzDistribution mfDistrib;
    SampledSpectrum eta, k;
};

// LayeredBxDFConfig Definition
struct LayeredBxDFConfig {
    uint8_t maxDepth = 10;
    uint8_t nSamples = 1;
    uint8_t twoSided = true;
};

// TopOrBottomBxDF Definition
template <typename TopBxDF, typename BottomBxDF>
class TopOrBottomBxDF {
  public:
    // TopOrBottomBxDF Public Methods
    TopOrBottomBxDF() = default;
    PBRT_CPU_GPU
    TopOrBottomBxDF &operator=(const TopBxDF *t) {
        top = t;
        bottom = nullptr;
        return *this;
    }
    PBRT_CPU_GPU
    TopOrBottomBxDF &operator=(const BottomBxDF *b) {
        bottom = b;
        top = nullptr;
        return *this;
    }

    PBRT_CPU_GPU
    SampledSpectrum f(const Vector3f &wo, const Vector3f &wi, TransportMode mode) const {
        return top ? top->f(wo, wi, mode) : bottom->f(wo, wi, mode);
    }

    PBRT_CPU_GPU
    BSDFSample Sample_f(const Vector3f &wo, Float uc, const Point2f &u,
                        TransportMode mode,
                        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        return top ? top->Sample_f(wo, uc, u, mode, sampleFlags)
                   : bottom->Sample_f(wo, uc, u, mode, sampleFlags);
    }

    PBRT_CPU_GPU
    Float PDF(const Vector3f &wo, const Vector3f &wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        return top ? top->PDF(wo, wi, mode, sampleFlags)
                   : bottom->PDF(wo, wi, mode, sampleFlags);
    }

    PBRT_CPU_GPU
    BxDFFlags Flags() const { return top ? top->Flags() : bottom->Flags(); }

  private:
    const TopBxDF *top = nullptr;
    const BottomBxDF *bottom = nullptr;
};

// LayeredBxDF Definition
template <typename TopBxDF, typename BottomBxDF, bool SupportAttenuation>
class LayeredBxDF {
  public:
    // LayeredBxDF Public Methods
    LayeredBxDF() = default;
    PBRT_CPU_GPU
    LayeredBxDF(TopBxDF top, BottomBxDF bottom, Float thickness,
                const SampledSpectrum &albedo, Float g, LayeredBxDFConfig config)
        : top(top),
          bottom(bottom),
          thickness(std::max(thickness, std::numeric_limits<Float>::min())),
          g(g),
          albedo(albedo),
          config(config) {}

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() {
        top.Regularize();
        bottom.Regularize();
    }

    PBRT_CPU_GPU
    bool SampledPDFIsProportional() const { return true; }

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        BxDFFlags topFlags = top.Flags(), bottomFlags = bottom.Flags();
        CHECK(IsTransmissive(topFlags) ||
              IsTransmissive(bottomFlags));  // otherwise, why bother?

        BxDFFlags flags = BxDFFlags::Reflection;
        if (IsSpecular(topFlags))
            flags = flags | BxDFFlags::Specular;

        if (IsDiffuse(topFlags) || IsDiffuse(bottomFlags) || albedo)
            flags = flags | BxDFFlags::Diffuse;
        else if (IsGlossy(topFlags) || IsGlossy(bottomFlags))
            flags = flags | BxDFFlags::Glossy;

        if (IsTransmissive(topFlags) && IsTransmissive(bottomFlags))
            flags = flags | BxDFFlags::Transmission;

        return flags;
    }

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        SampledSpectrum f(0.);
        // Set _wi_ and _wi_ for layered BSDF evaluation
        if (config.twoSided && wo.z < 0) {
            // BIG WIN
            wo = -wo;
            wi = -wi;
        }

        // Determine entrance and exit interfaces for layered BSDF
        bool enteredTop = wo.z > 0;
        TopOrBottomBxDF<TopBxDF, BottomBxDF> enterInterface, exitInterface;
        TopOrBottomBxDF<TopBxDF, BottomBxDF> nonExitInterface;
        if (enteredTop)
            enterInterface = &top;
        else
            enterInterface = &bottom;
        if (SameHemisphere(wo, wi) ^ enteredTop) {
            exitInterface = &bottom;
            nonExitInterface = &top;
        } else {
            exitInterface = &top;
            nonExitInterface = &bottom;
        }
        Float exitZ = (SameHemisphere(wo, wi) ^ enteredTop) ? 0 : thickness;

        // Account for reflection at the entrance interface
        if (SameHemisphere(wo, wi))
            f = config.nSamples * enterInterface.f(wo, wi, mode);

        // Declare _RNG_ for layered BSDF evaluation
        RNG rng(Hash(GetOptions().seed, wo), Hash(wi));
        auto r = [&rng]() {
            return std::min<Float>(rng.Uniform<Float>(), OneMinusEpsilon);
        };

        for (int s = 0; s < config.nSamples; ++s) {
            // Sample random walk through layers to estimate BSDF value
            // Sample transmission direction through entrance interface
            Float uc = r();
            Point2f u(r(), r());
            BSDFSample wos = enterInterface.Sample_f(wo, uc, u, mode,
                                                     BxDFReflTransFlags::Transmission);
            if (!wos || wos.wi.z == 0)
                continue;

            // Sample BSDF for NEE in _wi_'s direction
            uc = r();
            u = Point2f(r(), r());
            BSDFSample wis = exitInterface.Sample_f(wi, uc, u, ~mode,
                                                    BxDFReflTransFlags::Transmission);
            if (!wis || wis.wi.z == 0)
                continue;

            // Declare state for random walk through BSDF layers
            SampledSpectrum beta = wos.f * AbsCosTheta(wos.wi) / wos.pdf;
            SampledSpectrum betaExit = wis.f / wis.pdf;
            Vector3f w = wos.wi;
            Float z = enteredTop ? thickness : 0;
            HGPhaseFunction phase(g);

            for (int depth = 0; depth < config.maxDepth; ++depth) {
                // Sample next event for layered BSDF evaluation random walk
                VLOG(2, "beta: %s, w: %s, f: %s", beta, w, f);
                // Possibly terminate layered BSDF random walk with Russian Roulette
                if (depth > 3 && beta.MaxComponentValue() < .25) {
                    Float q = std::max<Float>(0, 1 - beta.MaxComponentValue());
                    if (r() < q)
                        break;
                    beta /= 1 - q;
                    VLOG(2, "After RR with q = %f, beta: %s", q, beta);
                }

                if (SupportAttenuation && albedo) {
                    // Sample medium scattering for layered BSDF evaluation
                    Float sigma_t = 1;
                    Float dz = SampleExponential(r(), sigma_t / AbsCosTheta(w));
                    Float zp = w.z > 0 ? (z + dz) : (z - dz);
                    CHECK_RARE(1e-5, z == zp);
                    if (z == zp)
                        continue;
                    if (0 < zp && zp < thickness) {
                        // Handle scattering event in layered BSDF medium
#if 0
// TODO: cancel out and simplify: should be
// f *= AbsCosTheta(w) / sigma_t (!!!) -- that in turn makes the tricky cosine stuff
// more reasonable / palatible...
//beta *= Tr(dz, w) / ExponentialPDF(dz, sigma_t / AbsCosTheta(w));
beta *= AbsCosTheta(w) / sigma_t;
// Tricky cosines. Always divide here since we always
// include it when we leave a surface.
beta /= AbsCosTheta(w);
#endif
                        // Account for scattering through _exitInterface_ using _wis_
                        Float wt = 1;
                        if (!IsSpecular(exitInterface.Flags()))
                            wt = PowerHeuristic(1, wis.pdf, 1, phase.PDF(-w, -wis.wi));
                        Float te = Tr(zp - exitZ, wis.wi);
                        f += beta * albedo * phase.p(-w, -wis.wi) * wt * te * betaExit;

                        // Sample phase function and update layered path state
                        PhaseFunctionSample ps = phase.Sample_p(-w, Point2f(r(), r()));
                        if (!ps || ps.wi.z == 0)
                            continue;
                        beta *= albedo * ps.p / ps.pdf;
                        w = ps.wi;
                        z = zp;

                        if (!IsSpecular(exitInterface.Flags())) {
                            // Account for scattering through _exitInterface_ from new _w_
                            SampledSpectrum fExit = exitInterface.f(-w, wi, mode);
                            if (fExit) {
                                Float exitPDF = exitInterface.PDF(
                                    -w, wi, mode, BxDFReflTransFlags::Transmission);
                                Float weight = PowerHeuristic(1, ps.pdf, 1, exitPDF);
                                f += beta * Tr(zp - exitZ, ps.wi) * fExit * weight;
                            }
                        }

                        continue;
                    }
                    z = Clamp(zp, 0, thickness);

                } else {
                    // Advance to next layer boundary and update _beta_ for transmittance
                    z = (z == thickness) ? 0 : thickness;
                    beta *= Tr(thickness, w);
                }
                if (z == exitZ) {
                    // Account for reflection at _exitInterface_
                    Float uc = r();
                    Point2f u(r(), r());
                    BSDFSample bs = exitInterface.Sample_f(
                        -w, uc, u, mode, BxDFReflTransFlags::Reflection);
                    if (!bs || bs.pdf == 0 || bs.wi.z == 0)
                        break;
                    beta *= bs.f * AbsCosTheta(bs.wi) / bs.pdf;
                    w = bs.wi;

                } else {
                    // Account for scattering at _nonExitInterface_
                    if (!IsSpecular(nonExitInterface.Flags())) {
                        // Add NEE contribution along pre-sampled _wis_ direction
                        Float wt = 1;
                        if (!IsSpecular(exitInterface.Flags()))
                            wt = PowerHeuristic(1, wis.pdf, 1,
                                                nonExitInterface.PDF(-w, -wis.wi, mode));
                        f += beta * nonExitInterface.f(-w, -wis.wi, mode) *
                             AbsCosTheta(wis.wi) * wt * Tr(thickness, wis.wi) * betaExit;
                    }
                    // Sample new direction using BSDF at _nonExitInterface_
                    Float uc = r();
                    Point2f u(r(), r());
                    BSDFSample bs = nonExitInterface.Sample_f(
                        -w, uc, u, mode, BxDFReflTransFlags::Reflection);
                    if (!bs || bs.wi.z == 0)
                        break;
                    beta *= bs.f * AbsCosTheta(bs.wi) / bs.pdf;
                    w = bs.wi;

                    if (!IsSpecular(exitInterface.Flags())) {
                        // Add NEE contribution along direction from BSDF sample
                        SampledSpectrum fExit = exitInterface.f(-w, wi, mode);
                        if (fExit) {
                            Float wt = 1;
                            if (!IsSpecular(nonExitInterface.Flags())) {
                                Float exitPDF = exitInterface.PDF(
                                    -w, wi, mode, BxDFReflTransFlags::Transmission);
                                wt = PowerHeuristic(1, bs.pdf, 1, exitPDF);
                            }
                            f += beta * Tr(thickness, bs.wi) * fExit * wt;
                        }
                    }
                }
            }
        }
        return f / config.nSamples;
    }

    PBRT_CPU_GPU
    BSDFSample Sample_f(Vector3f wo, Float uc, const Point2f &u, TransportMode mode,
                        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        CHECK(sampleFlags == BxDFReflTransFlags::All);  // for now
        // Set _wo_ for layered BSDF sampling
        bool flipWi = false;
        if (config.twoSided && wo.z < 0) {
            wo = -wo;
            flipWi = true;
        }

        // Sample BSDF at entrance interface to get initial direction _w_
        bool enteredTop = wo.z > 0;
        BSDFSample bs =
            enteredTop ? top.Sample_f(wo, uc, u, mode) : bottom.Sample_f(wo, uc, u, mode);
        if (!bs)
            return {};
        if (bs.IsReflection()) {
            if (flipWi)
                bs.wi = -bs.wi;
            return bs;
        }
        Vector3f w = bs.wi;

        // Declare _RNG_ for layered BSDF sampling
        RNG rng(Hash(GetOptions().seed, wo), Hash(uc, u));
        auto r = [&rng]() {
            return std::min<Float>(rng.Uniform<Float>(), OneMinusEpsilon);
        };

        // Declare common variables for layered BSDF sampling
        SampledSpectrum f = bs.f * AbsCosTheta(bs.wi);
        Float pdf = bs.pdf;
        Float z = enteredTop ? thickness : 0;
        HGPhaseFunction phase(g);

        for (int depth = 0; depth < config.maxDepth; ++depth) {
            // Follow random walk through layeres to sample layered BSDF
            // Possibly terminate layered BSDF sampling with Russian Roulette
            Float rrBeta = f.MaxComponentValue() / pdf;
            if (depth > 3 && rrBeta < 0.25) {
                Float q = std::max<Float>(0, 1 - rrBeta);
                if (r() < q)
                    return {};
                pdf *= 1 - q;
            }
            if (w.z == 0)
                return {};

            if (SupportAttenuation && albedo) {
                // Sample potential scattering event in layered medium
                Float sigma_t = 1;
                Float dz = SampleExponential(r(), sigma_t / AbsCosTheta(w));
                Float zp = w.z > 0 ? (z + dz) : (z - dz);
                CHECK_RARE(1e-5, zp == z);
                if (zp == z)
                    return {};
                if (0 < zp && zp < thickness) {
                    // Update path state for valid scattering event between interfaces
#if 0
// TODO: cancel out and simplify: should be
// f *= AbsCosTheta(w) / sigma_t (!!!) -- that in turn makes the tricky cosine stuff
// more reasonable / palatible...
//f *= Tr(dz, w) / ExponentialPDF(dz, sigma_t / AbsCosTheta(w));
f *= AbsCosTheta(w) / sigma_t;
// Tricky cosines. Always divide here since we always
// include it when we leave a surface.
f /= AbsCosTheta(w);
#endif
                    PhaseFunctionSample ps = phase.Sample_p(-w, Point2f(r(), r()));
                    if (!ps || ps.wi.z == 0)
                        return {};
                    f *= albedo * ps.p;
                    pdf *= ps.pdf;
                    w = ps.wi;
                    z = zp;

                    continue;
                }
                z = Clamp(zp, 0, thickness);
                if (z == 0)
                    DCHECK_LT(w.z, 0);
                else
                    DCHECK_GT(w.z, 0);

            } else {
                // Advance to the other layer interface
                // Bounce back and forth between the top and bottom
                z = (z == thickness) ? 0 : thickness;
                f *= Tr(thickness, w);
            }
            // Initialize _interface_ for current interface surface
            TopOrBottomBxDF<TopBxDF, BottomBxDF> interface;
            if (z == 0)
                interface = &bottom;
            else
                interface = &top;

            // Sample interface BSDF to determine new path direction
            Float uc = r();
            Point2f u(r(), r());
            BSDFSample bs = interface.Sample_f(-w, uc, u, mode);
            if (!bs || bs.wi.z == 0)
                return {};
            f *= bs.f;
            pdf *= bs.pdf;
            w = bs.wi;

            // Return _BSDFSample_ if path has left the layers
            if (bs.IsTransmission()) {
                BxDFFlags flags = SameHemisphere(wo, w) ? BxDFFlags::GlossyReflection
                                                        : BxDFFlags::GlossyTransmission;
                if (flipWi)
                    w = -w;
                return BSDFSample(f, w, pdf, flags);
            }

            // Scale _f_ by cosine term after scattering at the interface
            f *= AbsCosTheta(bs.wi);
        }
        return {};
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        CHECK(sampleFlags == BxDFReflTransFlags::All);  // for now
        // Set _wi_ and _wi_ for layered BSDF evaluation
        if (config.twoSided && wo.z < 0) {
            // BIG WIN
            wo = -wo;
            wi = -wi;
        }

        // Declare _RNG_ for layered BSDF evaluation
        RNG rng(Hash(GetOptions().seed, wo), Hash(wi));
        auto r = [&rng]() {
            return std::min<Float>(rng.Uniform<Float>(), OneMinusEpsilon);
        };

        bool enteredTop = wo.z > 0;
        Float pdfSum = 0;
        // Update _pdfSum_ for reflection at the entrance layer
        if (SameHemisphere(wo, wi)) {
            if (enteredTop)
                pdfSum += config.nSamples *
                          top.PDF(wo, wi, mode, BxDFReflTransFlags::Reflection);
            else
                pdfSum += config.nSamples *
                          bottom.PDF(wo, wi, mode, BxDFReflTransFlags::Reflection);
        }

        for (int s = 0; s < config.nSamples; ++s) {
            // Evaluate layered BSDF PDF sample
            if (SameHemisphere(wo, wi)) {
                // Evaluate TRT term for PDF estimate
                TopOrBottomBxDF<TopBxDF, BottomBxDF> rInterface, tInterface;
                if (enteredTop) {
                    rInterface = &bottom;
                    tInterface = &top;
                } else {
                    rInterface = &top;
                    tInterface = &bottom;
                }
                // Sample _tInterface_ to get direction into the layers
                Float uc = r();
                Point2f u(r(), r());
                BSDFSample wos = tInterface.Sample_f(wo, uc, u, mode);

                // Update _pdfSum_ accounting for TRT scattering events
                if (!wos || wos.wi.z == 0 || wos.IsReflection()) {
                    pdfSum += tInterface.PDF(wo, wi, mode);
                } else {
                    uc = r();
                    u = Point2f(r(), r());
                    BSDFSample wis = tInterface.Sample_f(wi, uc, u, ~mode);
                    if (!wis || wis.wi.z == 0 || wis.IsReflection())
                        continue;
                    // if (IsSpecular(tInterface.Flags()))
                    pdfSum += rInterface.PDF(-wos.wi, -wis.wi, mode);
                }

            } else {
                // Evaluate TT term for PDF estimate
                TopOrBottomBxDF<TopBxDF, BottomBxDF> toInterface, tiInterface;
                if (enteredTop) {
                    toInterface = &top;
                    tiInterface = &bottom;
                } else {
                    toInterface = &bottom;
                    tiInterface = &top;
                }

                Float uc = r();
                Point2f u(r(), r());
                BSDFSample wos = toInterface.Sample_f(wo, uc, u, mode);
                if (!wos || wos.wi.z == 0 || wos.IsReflection())
                    continue;

                uc = r();
                u = Point2f(r(), r());
                BSDFSample wis = tiInterface.Sample_f(wi, uc, u, ~mode);
                if (!wis || wis.wi.z == 0 || wis.IsReflection())
                    continue;

                if (IsSpecular(toInterface.Flags()))
                    pdfSum += tiInterface.PDF(-wos.wi, wi, mode);
                else if (IsSpecular(tiInterface.Flags()))
                    pdfSum += toInterface.PDF(wo, -wis.wi, mode);
                else
                    pdfSum += (toInterface.PDF(wo, -wis.wi, mode) +
                               tiInterface.PDF(-wos.wi, wi, mode)) /
                              2;
            }
        }
        // Return mixture of PDF estimate and constant PDF
        return Lerp(.9, 1 / (4 * Pi), pdfSum / config.nSamples);
    }

  protected:
    // LayeredBxDF Protected Methods
    PBRT_CPU_GPU
    static Float Tr(Float dz, const Vector3f &w) {
        if (std::abs(dz) <= std::numeric_limits<Float>::min())
            return 1;
        return std::exp(-std::abs(dz) / AbsCosTheta(w));
    }

    // LayeredBxDF Protected Members
    TopBxDF top;
    BottomBxDF bottom;
    Float thickness, g;
    SampledSpectrum albedo;
    LayeredBxDFConfig config;
};

// CoatedDiffuseBxDF Definition
class CoatedDiffuseBxDF
    : public LayeredBxDF<DielectricInterfaceBxDF, IdealDiffuseBxDF, false> {
  public:
    // CoatedDiffuseBxDF Public Methods
    using LayeredBxDF::LayeredBxDF;
    PBRT_CPU_GPU
    static constexpr const char *Name() { return "CoatedDiffuseBxDF"; }

    friend class SOA<CoatedDiffuseBxDF>;
};

// CoatedConductorBxDF Definition
class CoatedConductorBxDF
    : public LayeredBxDF<DielectricInterfaceBxDF, ConductorBxDF, false> {
  public:
    // CoatedConductorBxDF Public Methods
    PBRT_CPU_GPU
    static constexpr const char *Name() { return "CoatedConductorBxDF"; }
    using LayeredBxDF::LayeredBxDF;

    friend class SOA<CoatedConductorBxDF>;
};

// HairBxDF Definition
class HairBxDF {
  public:
    // HairBSDF Public Methods
    HairBxDF() = default;
    PBRT_CPU_GPU
    HairBxDF(Float h, Float eta, const SampledSpectrum &sigma_a, Float beta_m,
             Float beta_n, Float alpha);
    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const;
    PBRT_CPU_GPU
    BSDFSample Sample_f(Vector3f wo, Float uc, const Point2f &u, TransportMode mode,
                        BxDFReflTransFlags sampleFlags) const;
    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const;

    PBRT_CPU_GPU
    bool SampledPDFIsProportional() const { return false; }

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "HairBxDF"; }
    std::string ToString() const;

    PBRT_CPU_GPU
    BxDFFlags Flags() const { return BxDFFlags::GlossyReflection; }

    PBRT_CPU_GPU
    static RGBSpectrum SigmaAFromConcentration(Float ce, Float cp);
    PBRT_CPU_GPU
    static SampledSpectrum SigmaAFromReflectance(const SampledSpectrum &c, Float beta_n,
                                                 const SampledWavelengths &lambda);

  private:
    friend class SOA<HairBxDF>;
    // HairBSDF Constants
    static constexpr int pMax = 3;

    // HairBSDF Private Methods
    PBRT_CPU_GPU
    static Float Mp(Float cosTheta_i, Float cosTheta_o, Float sinTheta_i,
                    Float sinTheta_o, Float v) {
        Float a = cosTheta_i * cosTheta_o / v;
        Float b = sinTheta_i * sinTheta_o / v;
        Float mp =
            (v <= .1) ? (std::exp(LogI0(a) - b - 1 / v + 0.6931f + std::log(1 / (2 * v))))
                      : (std::exp(-b) * I0(a)) / (std::sinh(1 / v) * 2 * v);
        CHECK(!std::isinf(mp) && !std::isnan(mp));
        return mp;
    }

    PBRT_CPU_GPU
    static pstd::array<SampledSpectrum, pMax + 1> Ap(Float cosTheta_o, Float eta, Float h,
                                                     const SampledSpectrum &T) {
        pstd::array<SampledSpectrum, pMax + 1> ap;
        // Compute $p=0$ attenuation at initial cylinder intersection
        Float cosGamma_o = SafeSqrt(1 - h * h);
        Float cosTheta = cosTheta_o * cosGamma_o;
        Float f = FrDielectric(cosTheta, eta);
        ap[0] = SampledSpectrum(f);

        // Compute $p=1$ attenuation term
        ap[1] = Sqr(1 - f) * T;

        // Compute attenuation terms up to $p=_pMax_$
        for (int p = 2; p < pMax; ++p)
            ap[p] = ap[p - 1] * T * f;

        // Compute attenuation term accounting for remaining orders of scattering
        if (1.f - T * f)
            ap[pMax] = ap[pMax - 1] * f * T / (1.f - T * f);

        return ap;
    }

    PBRT_CPU_GPU
    static inline Float Phi(int p, Float gamma_o, Float gamma_t) {
        return 2 * p * gamma_t - 2 * gamma_o + p * Pi;
    }

    PBRT_CPU_GPU
    static inline Float Np(Float phi, int p, Float s, Float gamma_o, Float gamma_t) {
        Float dphi = phi - Phi(p, gamma_o, gamma_t);
        // Remap _dphi_ to $[-\pi,\pi]$
        while (dphi > Pi)
            dphi -= 2 * Pi;
        while (dphi < -Pi)
            dphi += 2 * Pi;

        return TrimmedLogistic(dphi, s, -Pi, Pi);
    }

    PBRT_CPU_GPU
    pstd::array<Float, pMax + 1> ComputeApPDF(Float cosThetaO) const;

    // HairBSDF Private Members
    Float h, gamma_o, eta;
    SampledSpectrum sigma_a;
    Float beta_m, beta_n;
    Float v[pMax + 1];
    Float s;
    Float sin2kAlpha[3], cos2kAlpha[3];
};

// MeasuredBxDF Definition
class MeasuredBxDF {
  public:
    // MeasuredBxDF Public Methods
    MeasuredBxDF() = default;
    PBRT_CPU_GPU
    MeasuredBxDF(const MeasuredBRDF *brdf, const SampledWavelengths &lambda)
        : brdf(brdf), lambda(lambda) {}

    static MeasuredBRDF *BRDFDataFromFile(const std::string &filename, Allocator alloc);

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const;

    PBRT_CPU_GPU
    BSDFSample Sample_f(Vector3f wo, Float uc, const Point2f &u, TransportMode mode,
                        BxDFReflTransFlags sampleFlags) const;
    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const;

    PBRT_CPU_GPU
    bool SampledPDFIsProportional() const { return false; }

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "MeasuredBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    BxDFFlags Flags() const { return (BxDFFlags::Reflection | BxDFFlags::Glossy); }

  private:
    friend class SOA<MeasuredBxDF>;
    // MeasuredBxDF Private Methods
    PBRT_CPU_GPU
    static Float u2theta(Float u) { return Sqr(u) * (Pi / 2.f); }
    PBRT_CPU_GPU
    static Float u2phi(Float u) { return (2.f * u - 1.f) * Pi; }
    PBRT_CPU_GPU
    static Float theta2u(Float theta) { return std::sqrt(theta * (2.f / Pi)); }
    PBRT_CPU_GPU
    static Float phi2u(Float phi) { return (phi + Pi) / (2.f * Pi); }

    // MeasuredBxDF Private Members
    const MeasuredBRDF *brdf;
    SampledWavelengths lambda;
};

// BSSRDFAdapter Definition
class BSSRDFAdapter {
  public:
    // BSSRDFAdapter Public Methods
    BSSRDFAdapter() = default;
    PBRT_CPU_GPU
    BSSRDFAdapter(Float eta) : eta(eta) {}

    PBRT_CPU_GPU
    BSDFSample Sample_f(const Vector3f &wo, Float uc, const Point2f &u,
                        TransportMode mode, BxDFReflTransFlags sampleFlags) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return {};

        // Cosine-sample the hemisphere, flipping the direction if necessary
        Vector3f wi = SampleCosineHemisphere(u);
        if (wo.z < 0)
            wi.z *= -1;
        return BSDFSample(f(wo, wi, mode), wi, PDF(wo, wi, mode, sampleFlags),
                          BxDFFlags::DiffuseReflection);
    }

    PBRT_CPU_GPU
    Float PDF(const Vector3f &wo, const Vector3f &wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return 0;
        return SameHemisphere(wo, wi) ? AbsCosTheta(wi) * InvPi : 0;
    }

    PBRT_CPU_GPU
    bool SampledPDFIsProportional() const { return false; }

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "BSSRDFAdapter"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return BxDFFlags(BxDFFlags::Reflection | BxDFFlags::Diffuse);
    }

    PBRT_CPU_GPU
    SampledSpectrum f(const Vector3f &wo, const Vector3f &wi, TransportMode mode) const {
        if (!SameHemisphere(wo, wi))
            return SampledSpectrum(0.f);
        // Compute $\Sw$ factor for BSSRDF value
        Float c = 1 - 2 * FresnelMoment1(1 / eta);
        SampledSpectrum f((1 - FrDielectric(CosTheta(wi), eta)) / (c * Pi));

        // Update BSSRDF transmission term to account for adjoint light transport
        if (mode == TransportMode::Radiance)
            f *= Sqr(eta);

        return f;
    }

  private:
    friend class SOA<BSSRDFAdapter>;
    // BSSRDFAdapter Private Members
    Float eta;
};

inline SampledSpectrum BxDFHandle::f(Vector3f wo, Vector3f wi, TransportMode mode) const {
    auto f = [&](auto ptr) -> SampledSpectrum { return ptr->f(wo, wi, mode); };
    return Dispatch(f);
}

inline BSDFSample BxDFHandle::Sample_f(Vector3f wo, Float uc, const Point2f &u,
                                       TransportMode mode,
                                       BxDFReflTransFlags sampleFlags) const {
    auto sample_f = [&](auto ptr) -> BSDFSample {
        return ptr->Sample_f(wo, uc, u, mode, sampleFlags);
    };
    return Dispatch(sample_f);
}

inline Float BxDFHandle::PDF(Vector3f wo, Vector3f wi, TransportMode mode,
                             BxDFReflTransFlags sampleFlags) const {
    auto pdf = [&](auto ptr) { return ptr->PDF(wo, wi, mode, sampleFlags); };
    return Dispatch(pdf);
}

inline bool BxDFHandle::SampledPDFIsProportional() const {
    auto approx = [&](auto ptr) { return ptr->SampledPDFIsProportional(); };
    return Dispatch(approx);
}

inline BxDFFlags BxDFHandle::Flags() const {
    auto flags = [&](auto ptr) { return ptr->Flags(); };
    return Dispatch(flags);
}

inline void BxDFHandle::Regularize() {
    auto regularize = [&](auto ptr) { ptr->Regularize(); };
    return Dispatch(regularize);
}

extern template class LayeredBxDF<DielectricInterfaceBxDF, IdealDiffuseBxDF, false>;
extern template class LayeredBxDF<DielectricInterfaceBxDF, ConductorBxDF, false>;

}  // namespace pbrt

#endif  // PBRT_BXDFS_H
