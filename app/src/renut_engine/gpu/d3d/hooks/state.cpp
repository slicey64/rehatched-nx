/**
 * @file    gpu/d3d/hooks/state.cpp
 * @brief   N&B's D3D state calls, hooked with the native ports (d3d_native.h).
 *
 * The render and sampler state setters are the ones N&B's state tables name
 * (D3D::InitializeApiState puts them into every device's dispatch table,
 * which the game's inline SetRenderState / SetSamplerState call through).
 * The rest are D3DDevice_Set* and the CDevice internals that write the ring.
 */
#include <cstring>

#include <rex/hook.h>

#include "renut_engine/gpu/banjo_renut_select.h"
#include <rex/types.h>

#include "renut_engine/gpu/d3d/d3d_native.h"
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/banjo_renut_tuning.h"  // blur-nx (2026-11-10, port 76; its key 213): OcclusionQueriesOn
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/render/surfaces.h"  // blur-nx (2026-11-10, port 76; its key 215): NoteSurfaceHeader
#include <algorithm>

using namespace renut::gpu;

namespace {

template <u32 State>
void SetRenderStateHook(u32 device, u32 value) {
  native::SetRenderState(native::DeviceAt(device), State, value);
}

template <u32 State>
void SetSamplerStateHook(u32 device, u32 sampler, u32 value) {
  native::SetSamplerState(native::DeviceAt(device), sampler, State, value);
}

template <u32 State>
u32 GetRenderStateHook(u32 device) {
  return native::GetRenderState(native::DeviceAt(device), State);
}

template <u32 State>
u32 GetSamplerStateHook(u32 device, u32 sampler) {
  return native::GetSamplerState(native::DeviceAt(device), sampler, State);
}

// The device's AddRef of a resource it hands out (D3DResource_AddRef,
// resource.cpp).
u32 AddRefResource(u32 resource) {
  return native::AddRefResource(resource);
}

// D3DSurface *GetRenderTarget(D3DDevice*, DWORD Index) [0x82636ED0]
u32 D3DDevice_GetRenderTarget_hook(u32 device, u32 index) {
  const native::Device d = native::DeviceAt(device);
  const u32 surface = d && index < 4 ? d.W(native::off::RenderTargets + index * 4) : 0;
  if (surface)
    AddRefResource(surface);
  return surface;
}

// D3DSurface *GetDepthStencilSurface(D3DDevice*) [0x82636F10]
u32 D3DDevice_GetDepthStencilSurface_hook(u32 device) {
  const native::Device d = native::DeviceAt(device);
  const u32 surface = d ? d.W(native::off::DepthStencil) : 0;
  if (surface)
    AddRefResource(surface);
  return surface;
}

// D3DVertexBuffer *GetStreamSource(D3DDevice*, UINT StreamNumber,
//     UINT *pOffsetInBytes, UINT *pStride) [0x82636E40]: the offset is the
// stream's fetch constant address less the buffer's.
u32 D3DDevice_GetStreamSource_hook(u32 device, u32 stream, mapped_u32 offset, mapped_u32 stride) {
  const native::Device d = native::DeviceAt(device);
  if (!d || stream >= 16)
    return 0;
  const u32 buffer = d.W(native::off::StreamSources + stream * 4);
  u32 bytes = 0;
  if (buffer) {
    const u32 fetch = d.W(native::off::TextureFetch + (95 - stream) * 8);
    bytes = fetch - native::GpuAddress(GuestPtr<const be_u32>(buffer)[6]);
    AddRefResource(buffer);
  }
  if (offset)
    *offset = bytes;
  if (stride)
    *stride = u32(d.B(native::off::StreamStrides + stream)) << 2;
  return buffer;
}

// void D3DDevice_GetViewport(D3DDevice*, D3DVIEWPORT9*) [0x82244B18]
void D3DDevice_GetViewport_hook(u32 device, mapped_u32 viewport) {
  const native::Device d = native::DeviceAt(device);
  if (!d || !viewport)
    return;
  for (u32 i = 0; i < 4; ++i)  // fctidz, the low word
    viewport[i] = u32(native::TruncateToI64(d.F(13024 + i * 4)));  // katamari-nx: 0x3170->0x3160
  viewport[4] = d.W(13040);  // katamari-nx: 0x3180->0x3170
  viewport[5] = d.W(13044);  // katamari-nx: 0x3184->0x3174
}

// void D3DDevice_SetClipPlane(D3DDevice*, DWORD Index, const float *pPlane) [0x826370E8]
void D3DDevice_SetClipPlane_hook(u32 device, u32 index, mapped_u32 plane) {
  native::Device d = native::DeviceAt(device);
  if (!d || !plane || index >= 6)
    return;
  for (u32 i = 0; i < 4; ++i)
    d.SetW(0x2820 + index * 16 + i * 4, plane[i]);
  d.Dirty(4, u64(1) << (54 - index));
}

// void D3DDevice_SetVertexFetchConstant(D3DDevice*, UINT VertexFetchRegister,
//     D3DVertexBuffer*, UINT Offset) [0x82637138]: a vertex fetch slot of the
// 3 in a fetch constant, made a vertex fetch constant first.
void D3DDevice_SetVertexFetchConstant_hook(u32 device, u32 reg, u32 vertex_buffer, u32 offset) {
  native::Device d = native::DeviceAt(device);
  const auto *vb = vertex_buffer ? GuestPtr<const be_u32>(vertex_buffer) : nullptr;
  if (!d || !vb || reg > 0x5F)
    return;
  const u32 slot = (0x5F - reg) / 3;
  const u32 part = (0x5F - reg) - slot * 3;
  const u32 constant = native::off::TextureFetch + slot * 24;
  const u32 type = d.W(constant) & 3;
  if (type == 0 || type == 2)
    std::memset(d.bytes + constant, 0, 24);
  d.TouchRange(constant, 24);  // banjo-nx (2026-10-26, banjo_renut_tags)
  d.SetW(constant, native::Insert(d.W(constant), 1, 0, 2));
  d.Dirty(3, u64(1) << (31 - slot));
  d.SetW(constant + part * 8, native::GpuAddress(u32(vb[6]) + offset));
  d.SetW(constant + part * 8 + 4, native::Insert(u32(vb[7]) - offset, 1, 28, 2));
}

// The boolean and integer shader constants (D3DDevice_Set{Vertex,Pixel}
// ShaderConstant{B,I}): bits from 0x2780 (vertex) / 0x2790 (pixel), a word
// of x, y, z bytes each from 0x27A0 / 0x27E0.
void SetShaderConstantsB(u32 device, u32 base, u32 start, mapped_u32 data, u32 count) {
  native::Device d = native::DeviceAt(device);
  if (!d || !data)
    return;
  for (u32 i = 0; i < count; ++i, ++start) {
    const u32 word = base + (start >> 5) * 4;
    const u32 bit = 1u << (start & 31);
    d.SetW(word, (d.W(word) & ~bit) | ((u32(data[i]) & 1) << (start & 31)));
  }
  d.Dirty(4, u64(1) << 56);
}

void SetShaderConstantsI(u32 device, u32 base, u32 start, mapped_u32 data, u32 count) {
  native::Device d = native::DeviceAt(device);
  if (!d || !data)
    return;
  for (u32 i = 0; i < count; ++i) {
    const u32 x = u32(data[i * 4]) & 0xFF, y = u32(data[i * 4 + 1]) & 0xFF;
    const u32 z = u32(data[i * 4 + 2]) & 0xFF;
    d.SetW(base + (start + i) * 4, (z << 16) | (y << 8) | x);
  }
  d.Dirty(4, u64(1) << 56);
}

// void D3DDevice_SetVertexShaderConstantB(D3DDevice*, UINT StartRegister,
//     const BOOL*, UINT BoolCount) [0x821F0B40], and the others alike.
void D3DDevice_SetVertexShaderConstantB_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsB(device, 0x2780, start, data, count);
}
void D3DDevice_SetPixelShaderConstantB_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsB(device, 0x2790, start, data, count);
}
void D3DDevice_SetVertexShaderConstantI_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsI(device, 0x27A0, start, data, count);
}
void D3DDevice_SetPixelShaderConstantI_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsI(device, 0x27E0, start, data, count);
}

// void D3DDevice_EndIndexedVertices(D3DDevice*) [0x8263ED90]: the inline end
// of BeginIndexedVertices, back to the ring pointer it saved.
void D3DDevice_EndIndexedVertices_hook(u32 device) {
  native::Device d = native::DeviceAt(device);
  if (d)
    d.SetW(native::off::Ring, d.W(13844));  // katamari-nx: 0x3484->0x3474
}

// void D3DDevice_SetTexture(D3DDevice*, DWORD Sampler, D3DBaseTexture*,
//     UINT64 PendingMask3) [0x821FE558]; the mask is the sampler's fetch
// constant bit, which the port derives from Sampler.
void D3DDevice_SetTexture_hook(u32 device, u32 sampler, u32 texture) {
  native::SetTexture(native::DeviceAt(device), sampler, texture);
}

// void D3DDevice_SetStreamSource(D3DDevice*, UINT StreamNumber, D3DVertexBuffer*,
//     UINT OffsetInBytes, UINT Stride, UINT64 PendingMask3) [0x821F9438]
void D3DDevice_SetStreamSource_hook(u32 device, u32 stream, u32 vertex_buffer, u32 offset,
                                    u32 stride) {
  native::SetStreamSource(native::DeviceAt(device), stream, vertex_buffer, offset, stride);
}

// void D3DDevice_SetIndices(D3DDevice*, D3DIndexBuffer*) [0x821DE438]
void D3DDevice_SetIndices_hook(u32 device, u32 index_buffer) {
  native::SetIndices(native::DeviceAt(device), index_buffer);
}

// void D3DDevice_SetVertexDeclaration(D3DDevice*, D3DVertexDeclaration*) [0x821D5060]
void D3DDevice_SetVertexDeclaration_hook(u32 device, u32 declaration) {
  native::SetVertexDeclaration(native::DeviceAt(device), declaration);
}

// void D3DDevice_SetViewport(D3DDevice*, const D3DVIEWPORT9*) [0x821F2090]
void D3DDevice_SetViewport_hook(u32 device, u32 viewport) {
  native::SetViewport(native::DeviceAt(device), viewport);
}

// void D3D::SetViewport(D3DDevice*, float X, float Y, float Width, float Height,
//     float MinZ, float MaxZ) [0x821F2110]
void D3D_SetViewport_hook(u32 device, float x, float y, float width, float height, float min_z,
                          float max_z) {
  native::SetViewport(native::DeviceAt(device), x, y, width, height, min_z, max_z);
}

// void D3DDevice_SetScissorRect(D3DDevice*, const RECT*) [0x821F2368]
void D3DDevice_SetScissorRect_hook(u32 device, mapped_u32 rect) {
  if (!rect)
    return;
  native::SetScissorRect(native::DeviceAt(device), i32(u32(rect[0])), i32(u32(rect[1])),
                         i32(u32(rect[2])), i32(u32(rect[3])));
}

// void D3D::SetSurfaceClip(CDevice*, int, int, int, int) [0x821F2460]: the
// window scissor and per-tile clip packets, nothing else.
void D3D_SetSurfaceClip_hook(u32 /*device*/, u32, u32, u32, u32) {}

// void D3DDevice_SetRenderTarget(D3DDevice*, DWORD Index, D3DSurface*) [0x82248BD0]
void D3DDevice_SetRenderTarget_hook(u32 device, u32 index, u32 surface) {
  NoteSurfaceHeader(surface);  // blur-nx (2026-11-10, port 76; its key 215: blur_renut_stack_surface_headers)
  native::SetRenderTarget(native::DeviceAt(device), index, surface);
}

// void D3DDevice_SetDepthStencilSurface(D3DDevice*, D3DSurface*) [0x82248FB0]
void D3DDevice_SetDepthStencilSurface_hook(u32 device, u32 surface) {
  NoteSurfaceHeader(surface);  // blur-nx (2026-11-10, port 76; its key 215: blur_renut_stack_surface_headers)
  native::SetDepthStencilSurface(native::DeviceAt(device), surface);
}

// void D3D::SetSurfaceInfo(CDevice*, D3DSurface*) [0x82248F20]
void D3D_SetSurfaceInfo_hook(u32 device, u32 surface) {
  native::SetSurfaceInfo(native::DeviceAt(device), surface);
}

// void D3DDevice_SetPredication(D3DDevice*, DWORD PredicationMask) [0x821F9A48]
void D3DDevice_SetPredication_hook(u32 device, u32 mask) {
  native::SetPredication(native::DeviceAt(device), mask);
}

// void D3DDevice_SetCommandBufferPredication(D3DDevice*, DWORD TilePredication,
//     DWORD RunPredication) [0x826411C0]
void D3DDevice_SetCommandBufferPredication_hook(u32 device, u32 tile_predication,
                                                u32 run_predication) {
  native::SetCommandBufferPredication(native::DeviceAt(device), tile_predication,
                                      run_predication);
}

// void D3DDevice_SetShaderGPRAllocation(D3DDevice*, DWORD Flags,
//     DWORD VertexShaderCount, DWORD PixelShaderCount) [0x821EFD38]
void D3DDevice_SetShaderGPRAllocation_hook(u32 device, u32 flags, u32 vertex_count,
                                           u32 pixel_count) {
  native::SetShaderGPRAllocation(native::DeviceAt(device), flags, vertex_count, pixel_count);
}

// void D3D::CleanupGprAllocationSetup(CDevice*) [0x82637208]
void D3D_CleanupGprAllocationSetup_hook(u32 device) {
  native::CleanupGprAllocationSetup(native::DeviceAt(device));
}

// void D3DDevice_SetScreenExtentQueryMode(D3DDevice*, D3DSCREENEXTENTQUERYMODE)
// [0x826414C0]
void D3DDevice_SetScreenExtentQueryMode_hook(u32 device, u32 mode) {
  native::SetScreenExtentQueryMode(native::DeviceAt(device), mode);
}

// void D3DDevice_UnsetAll(D3DDevice*) [0x82634C10]
void D3DDevice_UnsetAll_hook(u32 device) {
  native::UnsetAll(native::DeviceAt(device));
}

// void D3D::ResetAllState(CDevice*, BOOL) [0x82255C98]
void D3D_ResetAllState_hook(u32 device, u32 /*skip_packets*/) {
  native::ResetAllState(native::DeviceAt(device));
}

// void D3D::InvalidateConstants(D3DDevice*) [0x82634B90]
void D3D_InvalidateConstants_hook(u32 device) {
  native::InvalidateConstants(native::DeviceAt(device));
}

// void D3DDevice_BlockUntilIdle(D3DDevice*) [0x82640010] (IDA: the second
// D3D::InvalidateConstants): waits for the device's current fence (N&B calls
// it before releasing a device).
void D3DDevice_BlockUntilIdle_hook(u32 device) {
  native::InvalidateConstantsAndWait(native::DeviceAt(device));
}

} // namespace

// blur-nx (2026-11-10/11, port 76; its key 213: blur_renut_occlusion_queries): D3DQuery_GetData's answer. Renut runs no
// GPU packets, so the counts an occlusion query waits for never come: under Renut, Issue(END) leaves the sample blocks
// as they were (zeros, then whatever the memory held) and GetData said "not ready" to every query for ever, so whatever
// a game hides by a query was never drawn (Blur's light glows, the tail lights' red glow). AnswerOcclusionQuery, called
// before the game's own GetData runs, writes into every block of an issued occlusion query what the reference's GPU
// writes at the ZPASS event (the SDK's command_processor.cpp, ExecutePacketType3_EVENT_WRITE_ZPD): the block zeroed,
// Total_A and ZPass_A = 1000 samples passed (little-endian, as the GPU writes them), and clears the issue's fence word,
// which GetData compares with the GPU's read pointer: the counts are in.
// The query object as XDK 2.0.11164's GetData reads it (blur-nx, sub_8224CD70; D3DQuery_Issue is sub_8224CAE8, the two
// functions holding the 0xFFFFFEED sentinel): type 9 (occlusion) at +4, issued (bit 0x40) in the byte at +20, the fence
// at +24, up to 30 sample-block pointers from +28 and their number at +148, 64-byte blocks (END counts at +0, BEGIN at
// +32). A game at another XDK checks its own GetData against this before it registers it.
// Not hooked in the base: Beautiful Katamari's XDK 2.0.5632 image links no D3DQuery_Issue / GetData (no instruction or
// data word of it holds 0xFFFFFEED). A game whose image has GetData adds RENUT_D3DQUERY_GETDATA_HOOK(sub_<address>) to
// the hook list below (or calls AnswerOcclusionQuery from a GetData hook of its own), and runs post-codegen again
// (tools/post_codegen.py: direct_calls.py --redo) so GetData's callers go through the hooked entry.
namespace renut::gpu::native {
namespace {
constexpr u32 kQueryFakeSamples = 1000;  // the SDK's query_occlusion_fake_sample_count
u32 QueryBe(const u8 *p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | u32(p[3]); }
void QueryLe(u8 *p, u32 v) {
  p[0] = u8(v);
  p[1] = u8(v >> 8);
  p[2] = u8(v >> 16);
  p[3] = u8(v >> 24);
}
}  // namespace

// blur-nx (2026-11-11, port 76; its key 213): a query block's physical address as the game's GetData forms it from a
// pointer at query +28: (p & 0x1FFFFFFF) + (((p >> 20) + 0x200) & 0x1000), read through the 0xC0000000 mapping. A
// pointer in the 0xE0000000 range (Blur's blocks: E6D8DFC0) is a page further on (the console's 4 KB offset of that
// range). Answers written at the pointer's own page were never read: GetData read the sentinel there and said not ready.
u32 QueryBlockPhysical(u32 p) { return (p & 0x1FFFFFFFu) + ((((p >> 20) + 0x200u) & 0x1000u)); }

bool AnswerOcclusionQuery(u32 query) {
  if (!banjo::renut_gpu::OcclusionQueriesOn())
    return false;
  u8 *q = GuestPtr<u8>(query);
  if (!q || QueryBe(q + 4) != 9 || !(q[20] & 0x40))
    return false;
  const u32 blocks = std::min<u32>(QueryBe(q + 148), 30);
  bool answered = false;
  for (u32 i = 0; i < blocks; ++i) {
    const u32 pointer = QueryBe(q + 28 + 4 * i);
    if (!pointer)
      continue;
    // Every issued query, not only blocks holding the sentinel (under Renut they hold zeros or leftover memory): the
    // reference's answer for every query is 1000 passed.
    u8 *block = GuestPhysicalPtr<u8>(QueryBlockPhysical(pointer));
    std::memset(block, 0, 64);
    QueryLe(block + 0, kQueryFakeSamples);   // Total_A
    QueryLe(block + 16, kQueryFakeSamples);  // ZPass_A
    answered = true;
  }
  if (answered) {
    std::memset(q + 24, 0, 4);
    static uint32_t logged = 0;
    if (logged++ < 4)
      RGPU_INFO("[renut] occlusion query {:08X}: {} block(s) answered with {} samples passed, as the reference's GPU "
                "does (blur_renut_occlusion_queries)", query, blocks, kQueryFakeSamples);
  }
  return answered;
}
}  // namespace renut::gpu::native

// blur-nx (2026-11-10/11, port 76; its key 213): D3DQuery_GetData(query, data, size, flags) at the game's own address:
// the answer above, then the game's GetData (its __imp__), whose result the game gets. Under the emulated renderer the
// game's GetData alone (BANJO_RENUT_HOOK). One line in the hook list: RENUT_D3DQUERY_GETDATA_HOOK(sub_8224CD70);
// (blur-nx's address; the base registers none, see AnswerOcclusionQuery).
#define RENUT_D3DQUERY_GETDATA_HOOK(subroutine)                                                                   \
  REX_IMPORT(__imp__##subroutine, renut_query_get_data_original_##subroutine, u32(u32, u32, u32, u32));          \
  static u32 renut_query_get_data_##subroutine(u32 query, u32 data, u32 size, u32 flags) {                        \
    ::renut::gpu::native::AnswerOcclusionQuery(query);                                                            \
    return renut_query_get_data_original_##subroutine(query, data, size, flags);                                  \
  }                                                                                                               \
  BANJO_RENUT_HOOK(subroutine, renut_query_get_data_##subroutine)


// Render states, by their index in the table (0x8273C278).
BANJO_RENUT_HOOK(sub_82269A00, SetRenderStateHook<10>);
BANJO_RENUT_HOOK(sub_82269A70, SetRenderStateHook<11>);
BANJO_RENUT_HOOK(sub_82269A40, SetRenderStateHook<12>);
BANJO_RENUT_HOOK(sub_82269308, SetRenderStateHook<13>);
BANJO_RENUT_HOOK(sub_822692D8, SetRenderStateHook<14>);
BANJO_RENUT_HOOK(sub_82269370, SetRenderStateHook<15>);
BANJO_RENUT_HOOK(sub_82269700, SetRenderStateHook<16>);
BANJO_RENUT_HOOK(sub_82269828, SetRenderStateHook<17>);
BANJO_RENUT_HOOK(sub_82269490, SetRenderStateHook<18>);
BANJO_RENUT_HOOK(sub_82269520, SetRenderStateHook<19>);
BANJO_RENUT_HOOK(sub_82269400, SetRenderStateHook<20>);
BANJO_RENUT_HOOK(sub_82269620, SetRenderStateHook<21>);
BANJO_RENUT_HOOK(sub_82269690, SetRenderStateHook<22>);
BANJO_RENUT_HOOK(sub_822695B0, SetRenderStateHook<23>);
BANJO_RENUT_HOOK(sub_82269338, SetRenderStateHook<24>);
BANJO_RENUT_HOOK(sub_82269798, SetRenderStateHook<25>);
BANJO_RENUT_HOOK(sub_822697F8, SetRenderStateHook<26>);
BANJO_RENUT_HOOK(sub_82269AA8, SetRenderStateHook<27>);
BANJO_RENUT_HOOK(sub_82269AE8, SetRenderStateHook<28>);
BANJO_RENUT_HOOK(sub_82269B50, SetRenderStateHook<29>);
BANJO_RENUT_HOOK(sub_82269B88, SetRenderStateHook<30>);
BANJO_RENUT_HOOK(sub_82269BC0, SetRenderStateHook<31>);
BANJO_RENUT_HOOK(sub_82269B20, SetRenderStateHook<32>);
BANJO_RENUT_HOOK(sub_82269CC0, SetRenderStateHook<33>);
BANJO_RENUT_HOOK(sub_82269CE0, SetRenderStateHook<34>);
BANJO_RENUT_HOOK(sub_82269D00, SetRenderStateHook<35>);
BANJO_RENUT_HOOK(sub_82269C20, SetRenderStateHook<36>);
BANJO_RENUT_HOOK(sub_82269C58, SetRenderStateHook<37>);
BANJO_RENUT_HOOK(sub_82269C90, SetRenderStateHook<38>);
BANJO_RENUT_HOOK(sub_82269BF0, SetRenderStateHook<39>);
BANJO_RENUT_HOOK(sub_82269D20, SetRenderStateHook<40>);
BANJO_RENUT_HOOK(sub_82269D40, SetRenderStateHook<41>);
BANJO_RENUT_HOOK(sub_82269D60, SetRenderStateHook<42>);
BANJO_RENUT_HOOK(sub_82269D80, SetRenderStateHook<43>);
BANJO_RENUT_HOOK(sub_8226A0B0, SetRenderStateHook<44>);
BANJO_RENUT_HOOK(sub_8226A108, SetRenderStateHook<45>);
BANJO_RENUT_HOOK(sub_8226A0A0, SetRenderStateHook<46>);
BANJO_RENUT_HOOK(sub_8226A158, SetRenderStateHook<47>);
BANJO_RENUT_HOOK(sub_82269F50, SetRenderStateHook<48>);
BANJO_RENUT_HOOK(sub_82269F80, SetRenderStateHook<49>);
BANJO_RENUT_HOOK(sub_8226BEE0, SetRenderStateHook<50>);
BANJO_RENUT_HOOK(sub_82269DE0, SetRenderStateHook<51>);
BANJO_RENUT_HOOK(sub_82269EA8, SetRenderStateHook<52>);
BANJO_RENUT_HOOK(sub_82269FA0, SetRenderStateHook<53>);
BANJO_RENUT_HOOK(sub_82269FE0, SetRenderStateHook<54>);
BANJO_RENUT_HOOK(sub_8226A020, SetRenderStateHook<55>);
BANJO_RENUT_HOOK(sub_8226A060, SetRenderStateHook<56>);
BANJO_RENUT_HOOK(sub_8226A828, SetRenderStateHook<57>);
BANJO_RENUT_HOOK(sub_8226A7B8, SetRenderStateHook<58>);
BANJO_RENUT_HOOK(sub_8226A7F0, SetRenderStateHook<59>);
BANJO_RENUT_HOOK(sub_8226A1A8, SetRenderStateHook<60>);
BANJO_RENUT_HOOK(sub_8226A1C8, SetRenderStateHook<61>);
BANJO_RENUT_HOOK(sub_8226A1F0, SetRenderStateHook<62>);
BANJO_RENUT_HOOK(sub_8226A218, SetRenderStateHook<63>);
BANJO_RENUT_HOOK(sub_8226A240, SetRenderStateHook<64>);
BANJO_RENUT_HOOK(sub_8226A268, SetRenderStateHook<65>);
BANJO_RENUT_HOOK(sub_8226A290, SetRenderStateHook<66>);
BANJO_RENUT_HOOK(sub_8226A2B8, SetRenderStateHook<67>);
BANJO_RENUT_HOOK(sub_8226A2D8, SetRenderStateHook<68>);
BANJO_RENUT_HOOK(sub_8226A2F8, SetRenderStateHook<69>);
BANJO_RENUT_HOOK(sub_8226A320, SetRenderStateHook<70>);
BANJO_RENUT_HOOK(sub_8226A348, SetRenderStateHook<71>);
BANJO_RENUT_HOOK(sub_8226A370, SetRenderStateHook<72>);
BANJO_RENUT_HOOK(sub_8226A398, SetRenderStateHook<73>);
BANJO_RENUT_HOOK(sub_8226A3C0, SetRenderStateHook<74>);
BANJO_RENUT_HOOK(sub_8226A3E8, SetRenderStateHook<75>);
BANJO_RENUT_HOOK(sub_8226A508, SetRenderStateHook<76>);
BANJO_RENUT_HOOK(sub_8226A558, SetRenderStateHook<77>);
BANJO_RENUT_HOOK(sub_8226A5F0, SetRenderStateHook<78>);
BANJO_RENUT_HOOK(sub_8226A688, SetRenderStateHook<79>);
BANJO_RENUT_HOOK(sub_8226A720, SetRenderStateHook<80>);
BANJO_RENUT_HOOK(sub_8226A860, SetRenderStateHook<81>);
BANJO_RENUT_HOOK(sub_8226A898, SetRenderStateHook<82>);
BANJO_RENUT_HOOK(sub_8226A8C8, SetRenderStateHook<83>);
BANJO_RENUT_HOOK(sub_8226A8F0, SetRenderStateHook<84>);
BANJO_RENUT_HOOK(sub_8226A920, SetRenderStateHook<85>);
BANJO_RENUT_HOOK(sub_8226A948, SetRenderStateHook<86>);
BANJO_RENUT_HOOK(sub_8226A978, SetRenderStateHook<87>);
BANJO_RENUT_HOOK(sub_8226A9A8, SetRenderStateHook<88>);
BANJO_RENUT_HOOK(sub_8226A9D0, SetRenderStateHook<89>);
BANJO_RENUT_HOOK(sub_8226AA98, SetRenderStateHook<90>);
BANJO_RENUT_HOOK(sub_8226AAC8, SetRenderStateHook<91>);
BANJO_RENUT_HOOK(sub_8226AAF8, SetRenderStateHook<92>);
BANJO_RENUT_HOOK(sub_8226AB28, SetRenderStateHook<93>);
BANJO_RENUT_HOOK(sub_8226AB48, SetRenderStateHook<94>);
BANJO_RENUT_HOOK(sub_8226AB58, SetRenderStateHook<95>);
BANJO_RENUT_HOOK(sub_8226AA00, SetRenderStateHook<96>);
BANJO_RENUT_HOOK(sub_8226AA30, SetRenderStateHook<97>);
BANJO_RENUT_HOOK(sub_82269928, SetRenderStateHook<98>);
BANJO_RENUT_HOOK(sub_82269960, SetRenderStateHook<99>);
BANJO_RENUT_HOOK(sub_822699D0, SetRenderStateHook<100>);

// Sampler states, by their index in the table (0x8273C738).
BANJO_RENUT_HOOK(sub_8226B2F0, SetSamplerStateHook<0>);
BANJO_RENUT_HOOK(sub_8226B340, SetSamplerStateHook<1>);
BANJO_RENUT_HOOK(sub_8226B390, SetSamplerStateHook<2>);
BANJO_RENUT_HOOK(sub_8226B280, SetSamplerStateHook<3>);
BANJO_RENUT_HOOK(sub_8226AD20, SetSamplerStateHook<4>);
BANJO_RENUT_HOOK(sub_8226AB78, SetSamplerStateHook<5>);
BANJO_RENUT_HOOK(sub_8226AEC0, SetSamplerStateHook<6>);
BANJO_RENUT_HOOK(sub_8226B0E0, SetSamplerStateHook<7>);
BANJO_RENUT_HOOK(sub_8226B180, SetSamplerStateHook<8>);
BANJO_RENUT_HOOK(sub_8226AFC0, SetSamplerStateHook<9>);
BANJO_RENUT_HOOK(sub_8226AE20, SetSamplerStateHook<10>);
BANJO_RENUT_HOOK(sub_8226AC78, SetSamplerStateHook<11>);
BANJO_RENUT_HOOK(sub_8226AF18, SetSamplerStateHook<12>);
BANJO_RENUT_HOOK(sub_8226B200, SetSamplerStateHook<13>);
BANJO_RENUT_HOOK(sub_8226B3E0, SetSamplerStateHook<14>);
BANJO_RENUT_HOOK(sub_8226B038, SetSamplerStateHook<15>);
BANJO_RENUT_HOOK(sub_8226B438, SetSamplerStateHook<16>);
BANJO_RENUT_HOOK(sub_8226B490, SetSamplerStateHook<17>);
BANJO_RENUT_HOOK(sub_8226B4E8, SetSamplerStateHook<18>);
BANJO_RENUT_HOOK(sub_8226B540, SetSamplerStateHook<19>);

BANJO_RENUT_HOOK(sub_8226F2D0, D3DDevice_SetTexture_hook);  // D3DDevice_SetTexture
BANJO_RENUT_UNHOOKED_HOOK(sub_824A3D40, D3DDevice_SetTexture_hook);  // D3DDevice_SetTexture_Inline
BANJO_RENUT_HOOK(sub_8226B698, D3DDevice_SetStreamSource_hook);  // D3DDevice_SetStreamSource
BANJO_RENUT_HOOK(sub_8226B7B8, D3DDevice_SetIndices_hook);  // D3DDevice_SetIndices
BANJO_RENUT_HOOK(sub_8227AC78, D3DDevice_SetVertexDeclaration_hook);  // D3DDevice_SetVertexDeclaration (rehatched-nx: the store to +12240)
BANJO_RENUT_HOOK(sub_8226BEF0, D3DDevice_SetViewport_hook);  // D3DDevice_SetViewport
BANJO_RENUT_HOOK(sub_8226BC88, D3D_SetViewport_hook);  // D3D_SetViewport
BANJO_RENUT_HOOK(sub_8226B5A0, D3DDevice_SetScissorRect_hook);  // D3DDevice_SetScissorRect
BANJO_RENUT_UNHOOKED_HOOK(sub_824DB8C8, D3D_SetSurfaceClip_hook);  // D3D_SetSurfaceClip
BANJO_RENUT_HOOK(sub_8226C020, D3DDevice_SetRenderTarget_hook);  // D3DDevice_SetRenderTarget
BANJO_RENUT_HOOK(sub_8226C3B0, D3DDevice_SetDepthStencilSurface_hook);  // D3DDevice_SetDepthStencilSurface
BANJO_RENUT_UNHOOKED_HOOK(sub_824DE7D0, D3D_SetSurfaceInfo_hook);  // D3D_SetSurfaceInfo
BANJO_RENUT_HOOK(sub_822810E0, D3DDevice_SetPredication_hook);  // D3DDevice_SetPredication
BANJO_RENUT_UNHOOKED_HOOK_SHARED(sub_826411C0, D3DDevice_SetCommandBufferPredication_hook);  // D3DDevice_SetCommandBufferPredication
BANJO_RENUT_HOOK(sub_8227AE80, D3DDevice_SetShaderGPRAllocation_hook);  // D3DDevice_SetShaderGPRAllocation
BANJO_RENUT_UNHOOKED_HOOK(sub_824D3FE0, D3D_CleanupGprAllocationSetup_hook);  // D3D_CleanupGprAllocationSetup
BANJO_RENUT_HOOK(sub_822754D0, D3DDevice_SetScreenExtentQueryMode_hook);  // D3DDevice_SetScreenExtentQueryMode
BANJO_RENUT_HOOK(sub_8226CD98, D3DDevice_UnsetAll_hook);  // D3DDevice_UnsetAll
BANJO_RENUT_UNHOOKED_HOOK(sub_824DE3C0, D3D_ResetAllState_hook);  // D3D_ResetAllState
BANJO_RENUT_UNHOOKED_HOOK_SHARED(sub_82634B90, D3D_InvalidateConstants_hook);  // D3D_InvalidateConstants
BANJO_RENUT_UNHOOKED_HOOK(sub_824E0518, D3DDevice_BlockUntilIdle_hook);  // D3DDevice_BlockUntilIdle
BANJO_RENUT_UNHOOKED_HOOK(sub_82636EC8, D3DDevice_SetRenderTarget_hook);  // D3DDevice_SetRenderTarget_Thunk
BANJO_RENUT_UNHOOKED_HOOK(sub_82636ED0, D3DDevice_GetRenderTarget_hook);  // D3DDevice_GetRenderTarget
BANJO_RENUT_UNHOOKED_HOOK(sub_82636F10, D3DDevice_GetDepthStencilSurface_hook);  // D3DDevice_GetDepthStencilSurface
BANJO_RENUT_UNHOOKED_HOOK(sub_82636E40, D3DDevice_GetStreamSource_hook);  // D3DDevice_GetStreamSource
BANJO_RENUT_UNHOOKED_HOOK(sub_82244B18, D3DDevice_GetViewport_hook);  // D3DDevice_GetViewport
BANJO_RENUT_UNHOOKED_HOOK(sub_826370E8, D3DDevice_SetClipPlane_hook);  // D3DDevice_SetClipPlane
BANJO_RENUT_UNHOOKED_HOOK(sub_82637138, D3DDevice_SetVertexFetchConstant_hook);  // D3DDevice_SetVertexFetchConstant
BANJO_RENUT_UNHOOKED_HOOK(sub_824D4470, D3DDevice_SetVertexShaderConstantB_hook);  // D3DDevice_SetVertexShaderConstantB
BANJO_RENUT_UNHOOKED_HOOK(sub_824D44D0, D3DDevice_SetPixelShaderConstantB_hook);  // D3DDevice_SetPixelShaderConstantB
BANJO_RENUT_UNHOOKED_HOOK(sub_826374B0, D3DDevice_SetVertexShaderConstantI_hook);  // D3DDevice_SetVertexShaderConstantI
BANJO_RENUT_UNHOOKED_HOOK(sub_82637500, D3DDevice_SetPixelShaderConstantI_hook);  // D3DDevice_SetPixelShaderConstantI
BANJO_RENUT_UNHOOKED_HOOK(sub_8263ED90, D3DDevice_EndIndexedVertices_hook);  // D3DDevice_EndIndexedVertices

// Render state getters, by their index in the table (0x8273C278, field 0).
BANJO_RENUT_HOOK(sub_82269A38, GetRenderStateHook<10>);
BANJO_RENUT_HOOK(sub_82269A98, GetRenderStateHook<11>);
BANJO_RENUT_HOOK(sub_82269A60, GetRenderStateHook<12>);
BANJO_RENUT_HOOK(sub_82269328, GetRenderStateHook<13>);
BANJO_RENUT_HOOK(sub_822692F8, GetRenderStateHook<14>);
BANJO_RENUT_HOOK(sub_822693F0, GetRenderStateHook<15>);
BANJO_RENUT_HOOK(sub_82269788, GetRenderStateHook<16>);
BANJO_RENUT_HOOK(sub_822698B8, GetRenderStateHook<17>);
BANJO_RENUT_HOOK(sub_82269510, GetRenderStateHook<18>);
BANJO_RENUT_HOOK(sub_822695A0, GetRenderStateHook<19>);
BANJO_RENUT_HOOK(sub_82269480, GetRenderStateHook<20>);
BANJO_RENUT_HOOK(sub_82269680, GetRenderStateHook<21>);
BANJO_RENUT_HOOK(sub_822696F0, GetRenderStateHook<22>);
BANJO_RENUT_HOOK(sub_82269610, GetRenderStateHook<23>);
BANJO_RENUT_HOOK(sub_82269360, GetRenderStateHook<24>);
BANJO_RENUT_HOOK(sub_822697D0, GetRenderStateHook<25>);
BANJO_RENUT_HOOK(sub_82269818, GetRenderStateHook<26>);
BANJO_RENUT_HOOK(sub_82269AE0, GetRenderStateHook<27>);
BANJO_RENUT_HOOK(sub_82269B10, GetRenderStateHook<28>);
BANJO_RENUT_HOOK(sub_82269B78, GetRenderStateHook<29>);
BANJO_RENUT_HOOK(sub_82269BB0, GetRenderStateHook<30>);
BANJO_RENUT_HOOK(sub_82269BE0, GetRenderStateHook<31>);
BANJO_RENUT_HOOK(sub_82269B40, GetRenderStateHook<32>);
BANJO_RENUT_HOOK(sub_82269CD8, GetRenderStateHook<33>);
BANJO_RENUT_HOOK(sub_82269CF8, GetRenderStateHook<34>);
BANJO_RENUT_HOOK(sub_82269D18, GetRenderStateHook<35>);
BANJO_RENUT_HOOK(sub_82269C48, GetRenderStateHook<36>);
BANJO_RENUT_HOOK(sub_82269C80, GetRenderStateHook<37>);
BANJO_RENUT_HOOK(sub_82269CB0, GetRenderStateHook<38>);
BANJO_RENUT_HOOK(sub_82269C10, GetRenderStateHook<39>);
BANJO_RENUT_HOOK(sub_82269D38, GetRenderStateHook<40>);
BANJO_RENUT_HOOK(sub_82269D58, GetRenderStateHook<41>);
BANJO_RENUT_HOOK(sub_82269D78, GetRenderStateHook<42>);
BANJO_RENUT_HOOK(sub_82269DC8, GetRenderStateHook<43>);
BANJO_RENUT_HOOK(sub_8226A0F8, GetRenderStateHook<44>);
BANJO_RENUT_HOOK(sub_8226A148, GetRenderStateHook<45>);
BANJO_RENUT_HOOK(sub_8226A0A8, GetRenderStateHook<46>);
BANJO_RENUT_HOOK(sub_8226A198, GetRenderStateHook<47>);
BANJO_RENUT_HOOK(sub_82269F70, GetRenderStateHook<48>);
BANJO_RENUT_HOOK(sub_82269F98, GetRenderStateHook<49>);
BANJO_RENUT_HOOK(sub_82269DD8, GetRenderStateHook<50>);
BANJO_RENUT_HOOK(sub_82269E88, GetRenderStateHook<51>);
BANJO_RENUT_HOOK(sub_82269F40, GetRenderStateHook<52>);
BANJO_RENUT_HOOK(sub_82269FD8, GetRenderStateHook<53>);
BANJO_RENUT_HOOK(sub_8226A018, GetRenderStateHook<54>);
BANJO_RENUT_HOOK(sub_8226A058, GetRenderStateHook<55>);
BANJO_RENUT_HOOK(sub_8226A098, GetRenderStateHook<56>);
BANJO_RENUT_HOOK(sub_8226A850, GetRenderStateHook<57>);
BANJO_RENUT_HOOK(sub_8226A7E0, GetRenderStateHook<58>);
BANJO_RENUT_HOOK(sub_8226A818, GetRenderStateHook<59>);
BANJO_RENUT_HOOK(sub_8226A408, GetRenderStateHook<60>);
BANJO_RENUT_HOOK(sub_8226A418, GetRenderStateHook<61>);
BANJO_RENUT_HOOK(sub_8226A428, GetRenderStateHook<62>);
BANJO_RENUT_HOOK(sub_8226A438, GetRenderStateHook<63>);
BANJO_RENUT_HOOK(sub_8226A448, GetRenderStateHook<64>);
BANJO_RENUT_HOOK(sub_8226A458, GetRenderStateHook<65>);
BANJO_RENUT_HOOK(sub_8226A468, GetRenderStateHook<66>);
BANJO_RENUT_HOOK(sub_8226A478, GetRenderStateHook<67>);
BANJO_RENUT_HOOK(sub_8226A488, GetRenderStateHook<68>);
BANJO_RENUT_HOOK(sub_8226A498, GetRenderStateHook<69>);
BANJO_RENUT_HOOK(sub_8226A4A8, GetRenderStateHook<70>);
BANJO_RENUT_HOOK(sub_8226A4B8, GetRenderStateHook<71>);
BANJO_RENUT_HOOK(sub_8226A4C8, GetRenderStateHook<72>);
BANJO_RENUT_HOOK(sub_8226A4D8, GetRenderStateHook<73>);
BANJO_RENUT_HOOK(sub_8226A4E8, GetRenderStateHook<74>);
BANJO_RENUT_HOOK(sub_8226A4F8, GetRenderStateHook<75>);
BANJO_RENUT_HOOK(sub_8226A548, GetRenderStateHook<76>);
BANJO_RENUT_HOOK(sub_8226A5E8, GetRenderStateHook<77>);
BANJO_RENUT_HOOK(sub_8226A680, GetRenderStateHook<78>);
BANJO_RENUT_HOOK(sub_8226A718, GetRenderStateHook<79>);
BANJO_RENUT_HOOK(sub_8226A7B0, GetRenderStateHook<80>);
BANJO_RENUT_HOOK(sub_8226A888, GetRenderStateHook<81>);
BANJO_RENUT_HOOK(sub_8226A8B8, GetRenderStateHook<82>);
BANJO_RENUT_HOOK(sub_8226A8E8, GetRenderStateHook<83>);
BANJO_RENUT_HOOK(sub_8226A910, GetRenderStateHook<84>);
BANJO_RENUT_HOOK(sub_8226A940, GetRenderStateHook<85>);
BANJO_RENUT_HOOK(sub_8226A970, GetRenderStateHook<86>);
BANJO_RENUT_HOOK(sub_8226A9A0, GetRenderStateHook<87>);
BANJO_RENUT_HOOK(sub_8226A9C8, GetRenderStateHook<88>);
BANJO_RENUT_HOOK(sub_8226A9F8, GetRenderStateHook<89>);
BANJO_RENUT_HOOK(sub_8226AAB8, GetRenderStateHook<90>);
BANJO_RENUT_HOOK(sub_8226AAE8, GetRenderStateHook<91>);
BANJO_RENUT_HOOK(sub_8226AB18, GetRenderStateHook<92>);
BANJO_RENUT_HOOK(sub_8226AB40, GetRenderStateHook<93>);
BANJO_RENUT_HOOK(sub_8226AB50, GetRenderStateHook<94>);
BANJO_RENUT_HOOK(sub_8226AB68, GetRenderStateHook<95>);
BANJO_RENUT_HOOK(sub_8226AA20, GetRenderStateHook<96>);
BANJO_RENUT_HOOK(sub_8226AA88, GetRenderStateHook<97>);
BANJO_RENUT_HOOK(sub_82269950, GetRenderStateHook<98>);
BANJO_RENUT_HOOK(sub_822699A0, GetRenderStateHook<99>);
BANJO_RENUT_HOOK(sub_822699F0, GetRenderStateHook<100>);

// Sampler state getters (0x8273C738, field 0).
BANJO_RENUT_HOOK(sub_8226B328, GetSamplerStateHook<0>);
BANJO_RENUT_HOOK(sub_8226B378, GetSamplerStateHook<1>);
BANJO_RENUT_HOOK(sub_8226B3C8, GetSamplerStateHook<2>);
BANJO_RENUT_HOOK(sub_8226B2C8, GetSamplerStateHook<3>);
BANJO_RENUT_HOOK(sub_8226ADE8, GetSamplerStateHook<4>);
BANJO_RENUT_HOOK(sub_8226AC40, GetSamplerStateHook<5>);
BANJO_RENUT_HOOK(sub_8226AF00, GetSamplerStateHook<6>);
BANJO_RENUT_HOOK(sub_8226B140, GetSamplerStateHook<7>);
BANJO_RENUT_HOOK(sub_8226B1F0, GetSamplerStateHook<8>);
BANJO_RENUT_HOOK(sub_8226B028, GetSamplerStateHook<9>);
BANJO_RENUT_HOOK(sub_8226AEB0, GetSamplerStateHook<10>);
BANJO_RENUT_HOOK(sub_8226AD10, GetSamplerStateHook<11>);
BANJO_RENUT_HOOK(sub_8226AFB0, GetSamplerStateHook<12>);
BANJO_RENUT_HOOK(sub_8226B270, GetSamplerStateHook<13>);
BANJO_RENUT_HOOK(sub_8226B420, GetSamplerStateHook<14>);
BANJO_RENUT_HOOK(sub_8226B098, GetSamplerStateHook<15>);
BANJO_RENUT_HOOK(sub_8226B478, GetSamplerStateHook<16>);
BANJO_RENUT_HOOK(sub_8226B4D0, GetSamplerStateHook<17>);
BANJO_RENUT_HOOK(sub_8226B528, GetSamplerStateHook<18>);
BANJO_RENUT_HOOK(sub_8226B588, GetSamplerStateHook<19>);
