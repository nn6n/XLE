// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Rain.h"
#include "LightingParserContext.h"
#include "SceneParser.h"
#include "SceneEngineUtils.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ResourceBox.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Assets/DeferredShaderResource.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/Metal/ShaderResource.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../BufferUploads/DataPacket.h"
#include "../BufferUploads/ResourceLocator.h"
#include "../Utility/MemoryUtils.h"
#include "../Math/Transformations.h"
#include "../Math/ProjectionMath.h"
#include "../ConsoleRig/Console.h"

namespace SceneEngine
{
    void    Rain_Render(RenderCore::Metal::DeviceContext* context, 
                        LightingParserContext& parserContext)
    {
        CATCH_ASSETS_BEGIN
            using namespace RenderCore;

            auto cameraPosition = ExtractTranslation(parserContext.GetProjectionDesc()._cameraToWorld);

            const float rainBoxXYWidth = 30.f;
            const float rainBoxVerticalHeight = 15.f;

            float quantizedHeight = rainBoxVerticalHeight * XlCeil(cameraPosition[2] / rainBoxVerticalHeight);

                //  We draw the rain in sets of boxes, arranged around the camera. Let's build a grid
                //  of boxes, and cull them against the camera frustum. Many of the boxes will only be
                //  partially visible, but we can only cull on a per-box level.
            const int boxCountXY   = 4;
            const int boxCountZ    = 4;
            const int rainDropCountWidth = Tweakable("RainDropCountWidth", 64);

            Float3 boxCentrePts[boxCountXY*boxCountXY*boxCountZ];
            unsigned culledBoxCount = 0;

            static bool skipCull = false;

            for (int y=0; y<boxCountXY; ++y) {
                for (int x=0; x<boxCountXY; ++x) {
                    Float2 boxCentreXY(
                        cameraPosition[0] + ((x-(boxCountXY/2)) - 0.5f) * rainBoxXYWidth,
                        cameraPosition[1] + ((y-(boxCountXY/2)) - 0.5f) * rainBoxXYWidth);
                    for (int z=0; z<boxCountZ; ++z) {
                        float boxTopZ = quantizedHeight + ((z-(boxCountZ/2)+1) - 0.5f) * rainBoxVerticalHeight;

                        Float3 mins(boxCentreXY[0], boxCentreXY[1], boxTopZ - rainBoxVerticalHeight);
                        Float3 maxs(boxCentreXY[0] + rainBoxXYWidth, boxCentreXY[1] + rainBoxXYWidth, boxTopZ);

                        if (skipCull || !CullAABB_Aligned(parserContext.GetProjectionDesc()._worldToProjection, mins, maxs)) {
                            boxCentrePts[culledBoxCount++] = Float3(boxCentreXY[0], boxCentreXY[1], boxTopZ);
                        }
                    }
                }
            }

            if (!culledBoxCount) {
                return;
            }

            struct RainSpawnConstants
            {
                Float3 _spawnMidPoint; float _dummy;
                Float3 _fallingDirection; unsigned particleCountWidth;
                float _rainBoxXYWidth; float _rainBoxVerticalHeight; 
                float _dummy2[2];
            };
            Metal::ConstantBuffer cb0(nullptr, sizeof(RainSpawnConstants));

            auto& shader = ::Assets::GetAssetDep<Metal::ShaderProgram>(
                "game/xleres/effects/rainparticles.sh:vs_main:vs_*", 
                "game/xleres/effects/rainparticles.sh:gs_main:gs_*",
                "game/xleres/effects/rainparticles.sh:ps_main:ps_*",
                "");
            Metal::BoundUniforms uniforms(shader);
            Techniques::TechniqueContext::BindGlobalUniforms(uniforms);
            static uint64 HashRainSpawn = Hash64("RainSpawn");
            static uint64 HashRandomValuesTexture = Hash64("RandomValuesTexture");
            uniforms.BindConstantBuffer(HashRainSpawn, 0, 1);
            uniforms.BindShaderResource(HashRandomValuesTexture, 0, 1);

            auto& balancedNoise 
                = ::Assets::GetAssetDep<RenderCore::Assets::DeferredShaderResource>("game/xleres/DefaultResources/balanced_noise.dds:LT");

            const Metal::ConstantBuffer* cbs[]      = { &cb0 };
            const Metal::ShaderResourceView* srvs[] = { &balancedNoise.GetShaderResource() };
            uniforms.Apply(
                *context, parserContext.GetGlobalUniformsStream(), 
                Metal::UniformsStream(nullptr, cbs, dimof(cbs), srvs, dimof(srvs)));
            context->Bind(shader);
            context->Bind(Metal::BlendState(Metal::BlendOp::Add, Metal::Blend::SrcAlpha, Metal::Blend::InvSrcAlpha));
            context->Bind(Metal::CullMode::None);   // some particles will get a flipped winding order (because the projection is relatively simple... So disable back face culling)
            context->Bind(Metal::DepthStencilState(true, false));

            SetupVertexGeneratorShader(*context);
            context->Bind(Metal::Topology::PointList);

            for (unsigned b=0; b<culledBoxCount; ++b) {
                    // fewer particles in distant boxes?
                RainSpawnConstants rainSpawnConstaints = 
                {
                    boxCentrePts[b], 0.f,
                    Float3(8.1f, 0.1f, -12.f), rainDropCountWidth,
                    rainBoxXYWidth, rainBoxVerticalHeight,
                    0.f, 0.f
                };
                cb0.Update(*context, &rainSpawnConstaints, sizeof(rainSpawnConstaints));
                context->Draw(rainSpawnConstaints.particleCountWidth*rainSpawnConstaints.particleCountWidth);
            }
        CATCH_ASSETS_END(parserContext)

        context->Bind(RenderCore::Metal::Topology::TriangleList);
        context->Bind(RenderCore::Metal::CullMode::Back);
    }

    class SimRainResources
    {
    public:
        class Desc
        {
        public:
            unsigned _particleCountWidth;
            Desc(unsigned particleCountWidth) : _particleCountWidth(particleCountWidth) {}
        };

        intrusive_ptr<ID3D::Resource>                  _simParticlesBuffer;
        RenderCore::Metal::UnorderedAccessView      _simParticlesUAV;
        RenderCore::Metal::ShaderResourceView       _simParticlesSRV;

        SimRainResources(const Desc& desc);
        ~SimRainResources();
    };

    SimRainResources::SimRainResources(const Desc& desc)
    {
        using namespace RenderCore::Metal;
        using namespace BufferUploads;
        auto& uploads = GetBufferUploads();

        BufferDesc structuredBufferDesc;
        structuredBufferDesc._type = BufferDesc::Type::LinearBuffer;
        structuredBufferDesc._bindFlags = BindFlag::ShaderResource|BindFlag::StructuredBuffer|BindFlag::UnorderedAccess;
        structuredBufferDesc._cpuAccess = 0;
        structuredBufferDesc._gpuAccess = GPUAccess::Write;
        structuredBufferDesc._allocationRules = 0;
        const unsigned structureSize = 4*6;
        const unsigned bufferSize = structureSize*desc._particleCountWidth*desc._particleCountWidth;
        structuredBufferDesc._linearBufferDesc._sizeInBytes = bufferSize;
        structuredBufferDesc._linearBufferDesc._structureByteSize = structureSize;
        auto initialData = BufferUploads::CreateEmptyPacket(structuredBufferDesc);
        XlSetMemory(initialData->GetData(), 0, initialData->GetDataSize());
        auto simParticlesBuffer = uploads.Transaction_Immediate(structuredBufferDesc, initialData.get())->AdoptUnderlying();

        UnorderedAccessView simParticlesUAV(simParticlesBuffer.get());
        ShaderResourceView simParticlesSRV(simParticlesBuffer.get());

        _simParticlesBuffer = std::move(simParticlesBuffer);
        _simParticlesUAV = std::move(simParticlesUAV);
        _simParticlesSRV = std::move(simParticlesSRV);
    }

    SimRainResources::~SimRainResources()
    {}

    void    Rain_RenderSimParticles(RenderCore::Metal::DeviceContext* context, 
                                    LightingParserContext& parserContext,
                                    RenderCore::Metal::ShaderResourceView& depthsSRV,
                                    RenderCore::Metal::ShaderResourceView& normalsSRV)
    {
        CATCH_ASSETS_BEGIN
            using namespace RenderCore;
            const unsigned particleCountWidth = 64;
            auto& resources = Techniques::FindCachedBox<SimRainResources>(SimRainResources::Desc(particleCountWidth));

                //  we need to run a compute shader to update the position of these particles
                //  first, we have to unbind the depth buffer and create a shader resource view for it

            SavedTargets oldTargets(*context);
            if (!oldTargets.GetDepthStencilView()) {
                Throw(::Exceptions::BasicLabel("Missing depth stencil view when drawing rain particles"));
            }

            // auto depthBufferResource = Metal::ExtractResource<ID3D::Resource>(oldTargets.GetDepthStencilView());
            // Metal::ShaderResourceView depthsSRV(depthBufferResource.get(), Metal::NativeFormat::R24_UNORM_X8_TYPELESS);

            auto& simulationShaderByteCode = ::Assets::GetAssetDep<CompiledShaderByteCode>(
                "game/xleres/effects/simrain.sh:SimulateDrops:cs_*", 
                "");
            auto& simulationShader = ::Assets::GetAssetDep<Metal::ComputeShader>(
                "game/xleres/effects/simrain.sh:SimulateDrops:cs_*", 
                "");
            Metal::BoundUniforms simUniforms(simulationShaderByteCode);
            Techniques::TechniqueContext::BindGlobalUniforms(simUniforms);
            static uint64 HashSimulationParameters  = Hash64("SimulationParameters");
            static uint64 HashRandomValuesTexture   = Hash64("RandomValuesTexture");
            static uint64 HashParticlesInput        = Hash64("ParticlesInput");
            static uint64 HashDepthBuffer           = Hash64("DepthBuffer");
            static uint64 HashNormalsBuffer         = Hash64("NormalsBuffer");
            simUniforms.BindConstantBuffer(HashSimulationParameters, 0, 1);
            simUniforms.BindShaderResource(HashRandomValuesTexture, 0, 1);
            simUniforms.BindShaderResource(HashParticlesInput, 1, 1);
            simUniforms.BindShaderResource(HashDepthBuffer, 2, 1);
            simUniforms.BindShaderResource(HashNormalsBuffer, 3, 1);

            auto& projDesc = parserContext.GetProjectionDesc();
            Float3 projScale; float projZOffset;
            {
                    // this is a 4-parameter minimal projection transform
                    // see "RenderCore::Assets::PerspectiveProjection" for more detail
                const float n = projDesc._nearClip;
                const float f = projDesc._farClip;
                const float h = n * XlTan(.5f * projDesc._verticalFov);
                const float w = h * projDesc._aspectRatio;
                const float l = -w, r = w;
                const float t = h, b = -h;
                projScale[0] = (2.f * n) / (r-l);
                projScale[1] = (2.f * n) / (t-b);
                projScale[2] = -(f) / (f-n);
                projZOffset = -(f*n) / (f-n);
            }

            static unsigned s_timeRandomizer = 0;
            ++s_timeRandomizer;
            struct SimulationParameters
            {
                Float4x4    _worldToView;
                Float3      _projScale;
                float       _projZOffset;
	            Float3	    _averageRainVelocity;
	            float	    _elapsedTime;
	            int		    _particleCountWidth;
                unsigned    _timeRandomizer;
                float       _dummy[2];
            } simParam = {
                InvertOrthonormalTransform(projDesc._cameraToWorld),
                projScale, projZOffset,
                .5f * Float3(8.1f, 0.1f, -12.f),
                1.0f / 60.f, particleCountWidth,
                IntegerHash32(s_timeRandomizer), 0.f, 0.f
            };
            Metal::ConstantBuffer cb0(&simParam, sizeof(simParam));

            auto& balancedNoise 
                = ::Assets::GetAssetDep<RenderCore::Assets::DeferredShaderResource>("game/xleres/DefaultResources/balanced_noise.dds:LT");
            const Metal::ConstantBuffer* cbs[]      = { &cb0 };
            const Metal::ShaderResourceView* srvs[] = { &balancedNoise.GetShaderResource(), &resources._simParticlesSRV, &depthsSRV, &normalsSRV };
            
            context->Bind(simulationShader);
            context->BindCS(MakeResourceList(resources._simParticlesUAV));
            context->BindCS(MakeResourceList(Techniques::CommonResources()._defaultSampler));

            context->Bind(ResourceList<Metal::RenderTargetView, 0>(), nullptr);
            simUniforms.Apply(
                *context, parserContext.GetGlobalUniformsStream(), 
                Metal::UniformsStream(nullptr, cbs, dimof(cbs), srvs, dimof(srvs)));
            context->Dispatch(particleCountWidth/32, particleCountWidth/32);

            context->UnbindCS<Metal::UnorderedAccessView>(0, 1);
            context->UnbindCS<Metal::ShaderResourceView>(4, 2);
            oldTargets.ResetToOldTargets(*context);

                // After updating the particles, we can render them
                //  shader here is very similar to the non-sim rain. But we use a 
                //  different vertex shader that will read the particles from the simulation
                //  buffer shader resource;

            auto& shader = ::Assets::GetAssetDep<Metal::ShaderProgram>(
                "game/xleres/effects/simrain.sh:vs_main:vs_*", 
                "game/xleres/effects/rainparticles.sh:gs_main:gs_*",
                "game/xleres/effects/rainparticles.sh:ps_main:ps_*",
                "");
            Metal::BoundUniforms drawUniforms(shader);
            Techniques::TechniqueContext::BindGlobalUniforms(drawUniforms);
            drawUniforms.BindConstantBuffer(HashSimulationParameters, 0, 1);
            drawUniforms.BindShaderResource(HashRandomValuesTexture, 0, 1);
            drawUniforms.BindShaderResource(HashParticlesInput, 1, 1);
            drawUniforms.BindShaderResource(HashDepthBuffer, 2, 1);

            drawUniforms.Apply(
                *context, parserContext.GetGlobalUniformsStream(), 
                Metal::UniformsStream(nullptr, cbs, dimof(cbs), srvs, dimof(srvs)));
            context->Bind(shader);

            SetupVertexGeneratorShader(*context);
            context->Bind(Metal::BlendState(Metal::BlendOp::Add, Metal::Blend::SrcAlpha, Metal::Blend::InvSrcAlpha));
            context->Bind(Metal::CullMode::None);
            context->Bind(Metal::Topology::PointList);
            context->Bind(Metal::DepthStencilState(true, false));

            context->Draw(particleCountWidth*particleCountWidth);
            context->UnbindVS<Metal::ShaderResourceView>(3, 1);
        CATCH_ASSETS_END(parserContext)

        context->Bind(RenderCore::Metal::Topology::TriangleList);
        context->Bind(RenderCore::Metal::CullMode::Back);
    }



    static float Frac(float input) { return input - XlFloor(input); }

    void    SparkParticleTest_RenderSimParticles(   RenderCore::Metal::DeviceContext* context, 
                                                    LightingParserContext& parserContext,
                                                    RenderCore::Metal::ShaderResourceView& depthsSRV,
                                                    RenderCore::Metal::ShaderResourceView& normalsSRV)
    {
        CATCH_ASSETS_BEGIN
            using namespace RenderCore;
            const unsigned particleCountWidth = 128;
            auto& resources = Techniques::FindCachedBox<SimRainResources>(SimRainResources::Desc(particleCountWidth));

                //  we need to run a compute shader to update the position of these particles
                //  first, we have to unbind the depth buffer and create a shader resource view for it

            SavedTargets oldTargets(*context);
            if (!oldTargets.GetDepthStencilView()) {
                Throw(::Exceptions::BasicLabel("Missing depth stencil view when drawing rain particles"));
            }

            // auto depthBufferResource = Metal::ExtractResource<ID3D::Resource>(oldTargets.GetDepthStencilView());
            // Metal::ShaderResourceView depthsSRV(depthBufferResource.get(), NativeFormat::R24_UNORM_X8_TYPELESS);

            auto& simulationShaderByteCode = ::Assets::GetAssetDep<CompiledShaderByteCode>(
                "game/xleres/effects/sparkparticlestest.sh:SimulateDrops:cs_*", 
                "");
            auto& simulationShader = ::Assets::GetAssetDep<Metal::ComputeShader>(
                "game/xleres/effects/sparkparticlestest.sh:SimulateDrops:cs_*", 
                "");
            Metal::BoundUniforms simUniforms(simulationShaderByteCode);
            Techniques::TechniqueContext::BindGlobalUniforms(simUniforms);
            static uint64 HashSimulationParameters  = Hash64("SimulationParameters");
            static uint64 HashRandomValuesTexture   = Hash64("RandomValuesTexture");
            static uint64 HashParticlesInput        = Hash64("ParticlesInput");
            static uint64 HashDepthBuffer           = Hash64("DepthBuffer");
            static uint64 HashNormalsBuffer         = Hash64("NormalsBuffer");
            simUniforms.BindConstantBuffer(HashSimulationParameters, 0, 1);
            simUniforms.BindShaderResource(HashRandomValuesTexture, 0, 1);
            simUniforms.BindShaderResource(HashParticlesInput, 1, 1);
            simUniforms.BindShaderResource(HashDepthBuffer, 2, 1);
            simUniforms.BindShaderResource(HashNormalsBuffer, 3, 1);

            auto& projDesc = parserContext.GetProjectionDesc();
            Float3 projScale; float projZOffset;
            {
                    // this is a 4-parameter minimal projection transform
                    // see "RenderCore::Assets::PerspectiveProjection" for more detail
                const float n = projDesc._nearClip;
                const float f = projDesc._farClip;
                const float h = n * XlTan(.5f * projDesc._verticalFov);
                const float w = h * projDesc._aspectRatio;
                const float l = -w, r = w;
                const float t = h, b = -h;
                projScale[0] = (2.f * n) / (r-l);
                projScale[1] = (2.f * n) / (t-b);
                projScale[2] = -(f) / (f-n);
                projZOffset = -(f*n) / (f-n);
            }

            auto cameraPosition     = ExtractTranslation(parserContext.GetProjectionDesc()._cameraToWorld);
            auto cameraForward      = ExtractForward_Cam(parserContext.GetProjectionDesc()._cameraToWorld);
            Float3 spawnPosition    = cameraPosition + 5.f * cameraForward;

            float spawnAngle0 = 2.f * float(M_PI) * Frac(parserContext.GetSceneParser()->GetTimeValue() / 5.f);
            float spawnAngle1 = 2.f * float(M_PI) * Frac(parserContext.GetSceneParser()->GetTimeValue() / 7.5f);
            Float3 spawnDirection = Float3(
                XlCos(spawnAngle0) * XlCos(spawnAngle1),
                XlCos(spawnAngle0) * XlSin(spawnAngle1),
                XlSin(spawnAngle0));

            static unsigned s_timeRandomizer = 0;
            ++s_timeRandomizer;
            struct SimulationParameters
            {
                Float4x4    _worldToView;
                Float3      _projScale;
                float       _projZOffset;
                Float3      _spawnPosition;
                float       _dummy0;
	            Float3	    _spawnVelocity;
	            float	    _elapsedTime;
	            int		    _particleCountWidth;
                unsigned    _timeRandomizer;
                float       _dummy1[2];
            } simParam = {
                InvertOrthonormalTransform(projDesc._cameraToWorld),
                projScale, projZOffset,
                spawnPosition, 0.f,
                spawnDirection,
                1.0f / 60.f, particleCountWidth,
                IntegerHash32(s_timeRandomizer), 0.f, 0.f
            };
            Metal::ConstantBuffer cb0(&simParam, sizeof(simParam));

            auto& balancedNoise 
                = ::Assets::GetAssetDep<RenderCore::Assets::DeferredShaderResource>("game/xleres/DefaultResources/balanced_noise.dds:LT");
            const Metal::ConstantBuffer* cbs[]      = { &cb0 };
            const Metal::ShaderResourceView* srvs[] = { &balancedNoise.GetShaderResource(), &resources._simParticlesSRV, &depthsSRV, &normalsSRV };
            
            context->Bind(simulationShader);
            context->BindCS(MakeResourceList(resources._simParticlesUAV));
            context->BindCS(MakeResourceList(Techniques::CommonResources()._defaultSampler));

            context->Bind(ResourceList<Metal::RenderTargetView, 0>(), nullptr);
            simUniforms.Apply(
                *context, parserContext.GetGlobalUniformsStream(), 
                Metal::UniformsStream(nullptr, cbs, dimof(cbs), srvs, dimof(srvs)));
            context->Dispatch(particleCountWidth/32, particleCountWidth/32);

            context->UnbindCS<Metal::UnorderedAccessView>(0, 1);
            context->UnbindCS<Metal::ShaderResourceView>(4, 2);
            oldTargets.ResetToOldTargets(*context);

                // After updating the particles, we can render them
                //  shader here is very similar to the non-sim rain. But we use a 
                //  different vertex shader that will read the particles from the simulation
                //  buffer shader resource;

            auto& shader = ::Assets::GetAssetDep<Metal::ShaderProgram>(
                "game/xleres/effects/sparkparticlestest.sh:vs_main:vs_*", 
                "game/xleres/effects/sparkparticlestest.sh:gs_main:gs_*",
                "game/xleres/effects/sparkparticlestest.sh:ps_main:ps_*",
                "");
            Metal::BoundUniforms drawUniforms(shader);
            Techniques::TechniqueContext::BindGlobalUniforms(drawUniforms);
            drawUniforms.BindConstantBuffer(HashSimulationParameters, 0, 1);
            drawUniforms.BindShaderResource(HashRandomValuesTexture, 0, 1);
            drawUniforms.BindShaderResource(HashParticlesInput, 1, 1);
            drawUniforms.BindShaderResource(HashDepthBuffer, 2, 1);

            drawUniforms.Apply(
                *context, parserContext.GetGlobalUniformsStream(), 
                Metal::UniformsStream(nullptr, cbs, dimof(cbs), srvs, dimof(srvs)));
            context->Bind(shader);

            SetupVertexGeneratorShader(*context);
            context->Bind(Metal::BlendState(Metal::BlendOp::Add, Metal::Blend::One, Metal::Blend::One));
            context->Bind(Metal::CullMode::None);
            context->Bind(Metal::Topology::PointList);
            context->Bind(Metal::DepthStencilState(true, false));

            context->Draw(particleCountWidth*particleCountWidth);

            context->UnbindVS<Metal::ShaderResourceView>(3, 1);
        CATCH_ASSETS_END(parserContext)

        context->Bind(RenderCore::Metal::Topology::TriangleList);
        context->Bind(RenderCore::Metal::CullMode::Back);
    }


}

