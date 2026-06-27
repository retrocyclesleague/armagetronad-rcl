#include "config.h"

#ifndef DEDICATED

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL3/SDL_metal.h>

#include "rMetalBackend.h"
#include "rMetalGLCompat.h"
#include "rScreen.h"

static SDL_MetalView sr_metalView = NULL;
static id<MTLDevice> sr_metalDevice = nil;
static id<MTLCommandQueue> sr_metalQueue = nil;
static id<MTLRenderPipelineState> sr_colorPipeline = nil;
static id<MTLRenderPipelineState> sr_texPipeline = nil;
static id<MTLRenderPipelineState> sr_linePipeline = nil;
static id<MTLDepthStencilState> sr_depthOn = nil;
static id<MTLDepthStencilState> sr_depthOff = nil;
static id<MTLSamplerState> sr_sampler = nil;
static id<MTLSamplerState> sr_samplerRepeatS = nil;
static id<MTLTexture> sr_whiteTex = nil;

static id<MTLCommandBuffer> sr_frameCmd = nil;
static id<MTLRenderCommandEncoder> sr_frameEnc = nil;
static id<CAMetalDrawable> sr_frameDrawable = nil;
static id<MTLTexture> sr_depthTex = nil;
static NSUInteger sr_depthW = 0;
static NSUInteger sr_depthH = 0;

static NSMutableDictionary *sr_metalTextures = nil;

static CAMetalLayer *sr_MetalLayer()
{
    if (!sr_metalView)
        return nil;
    return (__bridge CAMetalLayer *)SDL_Metal_GetLayer(sr_metalView);
}

static const char *sr_MetalShaderSource()
{
    return R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 texcoord [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 texcoord;
};

struct Uniforms {
    float4x4 mvp;
    float4x4 texMatrix;
    int useTexture;
};

vertex VertexOut aa_vertex(VertexIn in [[stage_in]],
                           constant Uniforms& u [[buffer(1)]]) {
    VertexOut out;
    float4 pos = u.mvp * float4(in.position, 1.0);
    out.position = pos;
    out.color = in.color;
    float4 tc = u.texMatrix * float4(in.texcoord, 0.0, 1.0);
    out.texcoord = tc.xy;
    return out;
}

fragment float4 aa_fragment_color(VertexOut in [[stage_in]]) {
    return in.color;
}

fragment float4 aa_fragment_tex(VertexOut in [[stage_in]],
                                texture2d<float> tex [[texture(0)]],
                                sampler smp [[sampler(0)]]) {
    float4 s = tex.sample(smp, in.texcoord);
    return in.color * s;
}
)";
}

static id<MTLRenderPipelineState> sr_BuildPipeline(id<MTLLibrary> lib, NSString *fragName, MTLPixelFormat colorFormat)
{
    id<MTLFunction> vertexFn = [lib newFunctionWithName:@"aa_vertex"];
    id<MTLFunction> fragFn = [lib newFunctionWithName:fragName];
    MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = vertexFn;
    desc.fragmentFunction = fragFn;
    desc.colorAttachments[0].pixelFormat = colorFormat;
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
    vd.attributes[0].format = MTLVertexFormatFloat3;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat4;
    vd.attributes[1].offset = 12;
    vd.attributes[1].bufferIndex = 0;
    vd.attributes[2].format = MTLVertexFormatFloat2;
    vd.attributes[2].offset = 28;
    vd.attributes[2].bufferIndex = 0;
    vd.layouts[0].stride = 36;
    desc.vertexDescriptor = vd;

    NSError *err = nil;
    id<MTLRenderPipelineState> ps = [sr_metalDevice newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!ps)
        NSLog(@"Metal pipeline error: %@", err);
    return ps;
}

static void sr_MetalEnsurePipelines(MTLPixelFormat colorFormat)
{
    if (sr_colorPipeline)
        return;

    NSError *err = nil;
    id<MTLLibrary> lib = [sr_metalDevice newLibraryWithSource:@(sr_MetalShaderSource()) options:nil error:&err];
    if (!lib)
    {
        NSLog(@"Metal shader compile: %@", err);
        return;
    }

    sr_colorPipeline = sr_BuildPipeline(lib, @"aa_fragment_color", colorFormat);
    sr_texPipeline = sr_BuildPipeline(lib, @"aa_fragment_tex", colorFormat);
    sr_linePipeline = sr_BuildPipeline(lib, @"aa_fragment_color", colorFormat);

    MTLDepthStencilDescriptor *dOn = [MTLDepthStencilDescriptor new];
    dOn.depthCompareFunction = MTLCompareFunctionLessEqual;
    dOn.depthWriteEnabled = YES;
    sr_depthOn = [sr_metalDevice newDepthStencilStateWithDescriptor:dOn];

    MTLDepthStencilDescriptor *dOff = [MTLDepthStencilDescriptor new];
    dOff.depthCompareFunction = MTLCompareFunctionAlways;
    dOff.depthWriteEnabled = NO;
    sr_depthOff = [sr_metalDevice newDepthStencilStateWithDescriptor:dOff];

    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sr_sampler = [sr_metalDevice newSamplerStateWithDescriptor:sd];

    MTLSamplerDescriptor *sdRepeat = [MTLSamplerDescriptor new];
    sdRepeat.minFilter = MTLSamplerMinMagFilterLinear;
    sdRepeat.magFilter = MTLSamplerMinMagFilterLinear;
    sdRepeat.sAddressMode = MTLSamplerAddressModeRepeat;
    sdRepeat.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sr_samplerRepeatS = [sr_metalDevice newSamplerStateWithDescriptor:sdRepeat];

    uint8_t white[4] = {255, 255, 255, 255};
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:1 height:1 mipmapped:NO];
    sr_whiteTex = [sr_metalDevice newTextureWithDescriptor:td];
    [sr_whiteTex replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:white bytesPerRow:4];
}

static void sr_MetalEnsureDepth(NSUInteger w, NSUInteger h)
{
    if (sr_depthTex && sr_depthW == w && sr_depthH == h)
        return;
    sr_depthW = w;
    sr_depthH = h;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                  width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    sr_depthTex = [sr_metalDevice newTextureWithDescriptor:td];
}

static void sr_MetalEndEncoder()
{
    if (sr_frameEnc)
    {
        [sr_frameEnc endEncoding];
        sr_frameEnc = nil;
    }
}

bool sr_CreateMetalContext()
{
    if (!sr_window)
        return false;

    sr_metalView = SDL_Metal_CreateView(sr_window);
    if (!sr_metalView)
        return false;

    sr_metalDevice = MTLCreateSystemDefaultDevice();
    if (!sr_metalDevice)
        return false;

    sr_metalQueue = [sr_metalDevice newCommandQueue];

    CAMetalLayer *layer = sr_MetalLayer();
    if (!layer)
        return false;

    layer.device = sr_metalDevice;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    return true;
}

void sr_DestroyMetalContext()
{
    sr_MetalEndEncoder();
    sr_frameCmd = nil;
    sr_frameDrawable = nil;
    sr_depthTex = nil;
    sr_metalTextures = nil;
    sr_colorPipeline = nil;
    sr_texPipeline = nil;
    sr_linePipeline = nil;

    if (sr_metalView)
    {
        SDL_Metal_DestroyView(sr_metalView);
        sr_metalView = NULL;
    }
    sr_metalQueue = nil;
    sr_metalDevice = nil;
}

bool sr_MetalUploadTexture(GLuint texId, const void *pixels, int width, int height, bool rgba)
{
    if (!sr_metalDevice || !pixels || width <= 0 || height <= 0)
        return false;

    if (!sr_metalTextures)
        sr_metalTextures = [NSMutableDictionary dictionary];

    MTLPixelFormat fmt = rgba ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatBGRA8Unorm;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                                                  width:(NSUInteger)width
                                                                                 height:(NSUInteger)height
                                                                              mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [sr_metalDevice newTextureWithDescriptor:td];
    [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height)
            mipmapLevel:0
              withBytes:pixels
            bytesPerRow:(NSUInteger)(4 * width)];

    sr_metalTextures[@(texId)] = tex;
    return true;
}

void sr_MetalDeleteTexture(GLuint texId)
{
    [sr_metalTextures removeObjectForKey:@(texId)];
}

void sr_MetalBeginFrame()
{
    if (!sr_metalQueue)
        return;

    sr_MetalEndEncoder();
    sr_frameCmd = nil;
    sr_frameDrawable = nil;

    CAMetalLayer *layer = sr_MetalLayer();
    if (!layer)
        return;

    layer.device = sr_metalDevice;
    const double scale = layer.contentsScale > 0 ? layer.contentsScale : 1.0;
    const CGSize bounds = layer.bounds.size;
    const NSUInteger dw = (NSUInteger)(bounds.width * scale);
    const NSUInteger dh = (NSUInteger)(bounds.height * scale);
    layer.drawableSize = CGSizeMake(dw, dh);

    sr_MetalEnsurePipelines(layer.pixelFormat);
    sr_MetalEnsureDepth(dw, dh);

    sr_frameDrawable = [layer nextDrawable];
    if (!sr_frameDrawable)
        return;

    sr_frameCmd = [sr_metalQueue commandBuffer];

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = sr_frameDrawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.04, 0.10, 1.0);
    pass.depthAttachment.texture = sr_depthTex;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;

    sr_frameEnc = [sr_frameCmd renderCommandEncoderWithDescriptor:pass];
}

static void sr_MetalApplyViewport()
{
    const rMetalGLState *gs = sr_GetMetalGLState();
    if (!sr_frameEnc)
        return;
    MTLViewport vp;
    vp.originX = (double)gs->viewportX;
    vp.originY = (double)gs->viewportY;
    vp.width = (double)gs->viewportW;
    vp.height = (double)gs->viewportH;
    vp.znear = 0.0;
    vp.zfar = 1.0;
    [sr_frameEnc setViewport:vp];
}

static void sr_MetalDrawInternal(
    const float *vertices, int vertexCount,
    const float *mvp, const float *texMatrix,
    bool useTexture, GLuint textureId,
    MTLPrimitiveType prim, id<MTLRenderPipelineState> pipeline)
{
    if (!sr_frameEnc || vertexCount < 1 || !vertices)
        return;

    if (!sr_frameDrawable)
        sr_MetalBeginFrame();
    if (!sr_frameEnc)
        return;

    sr_MetalApplyViewport();

    const rMetalGLState *gs = sr_GetMetalGLState();
    [sr_frameEnc setDepthStencilState:gs->depthTest ? sr_depthOn : sr_depthOff];
    [sr_frameEnc setRenderPipelineState:pipeline];

    struct Uniforms {
        float mvp[16];
        float texMatrix[16];
        int useTexture;
    } uniforms;

    memcpy(uniforms.mvp, mvp, 16 * sizeof(float));
    memcpy(uniforms.texMatrix, texMatrix, 16 * sizeof(float));
    uniforms.useTexture = useTexture ? 1 : 0;
    [sr_frameEnc setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];

    const NSUInteger byteLen = (NSUInteger)vertexCount * 36;
    id<MTLBuffer> vbuf = [sr_metalDevice newBufferWithBytes:vertices length:byteLen options:MTLResourceStorageModeShared];
    [sr_frameEnc setVertexBuffer:vbuf offset:0 atIndex:0];

    if (useTexture)
    {
        id<MTLTexture> tex = sr_whiteTex;
        id<MTLTexture> bound = sr_metalTextures[@(textureId)];
        if (bound)
            tex = bound;
        [sr_frameEnc setFragmentTexture:tex atIndex:0];
        id<MTLSamplerState> smp = gs->texWrapSRepeat ? sr_samplerRepeatS : sr_sampler;
        [sr_frameEnc setFragmentSamplerState:smp atIndex:0];
    }

    [sr_frameEnc drawPrimitives:prim vertexStart:0 vertexCount:(NSUInteger)vertexCount];
}

void sr_MetalDrawTriangles(
    const float *vertices, int vertexCount,
    const float *mvp, const float *texMatrix,
    bool useTexture, GLuint textureId)
{
    id<MTLRenderPipelineState> pipe = useTexture ? sr_texPipeline : sr_colorPipeline;
    sr_MetalDrawInternal(vertices, vertexCount, mvp, texMatrix, useTexture, textureId,
                         MTLPrimitiveTypeTriangle, pipe);
}

void sr_MetalDrawLines(
    const float *vertices, int vertexCount,
    const float *mvp)
{
    float texIdentity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    sr_MetalDrawInternal(vertices, vertexCount, mvp, texIdentity, false, 0,
                         MTLPrimitiveTypeLine, sr_linePipeline);
}

void sr_MetalPresent()
{
    sr_MetalEndEncoder();

    if (sr_frameCmd && sr_frameDrawable)
    {
        [sr_frameCmd presentDrawable:sr_frameDrawable];
        [sr_frameCmd commit];
    }

    sr_frameCmd = nil;
    sr_frameDrawable = nil;
}

#endif
