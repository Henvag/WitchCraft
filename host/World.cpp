#include "World.h"

#include "Link.h"
#include "RenderTiming.h"
#include "MotionTimeline.h"
#include "OcclusionMesh.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace witchcraft
{
	namespace
	{
		using Vertex = skycraft::proto::RenVertex;

		constexpr std::uint32_t kFlagCutout = 1;
		constexpr std::uint32_t kFlagTranslucent = 2;
		constexpr float         kPi = 3.14159265f;

		constexpr char kShader[] = R"(
cbuffer Frame : register(b0)
{
	row_major float4x4 viewProj;
	float4 mode;  // x: 1 when this pass writes premultiplied translucent pixels
};
cbuffer Object : register(b1)
{
	float4 origin;  // camera-relative Minecraft position added to every vertex
};
Texture2D atlas : register(t0);
SamplerState mipSampler : register(s0);
SamplerState pointSampler : register(s1);

struct VSIn
{
	float3 pos : POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
	uint light : TEXCOORD1;
	uint flags : TEXCOORD2;
};
struct VSOut
{
	float4 pos : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
	float2 light : TEXCOORD1;
	nointerpolation uint flags : TEXCOORD2;
};

float Curve(float l) { return l / (4.0 - 3.0 * l); }

// index 0: no face. Then Minecraft's Direction ordinal + 1: down, up, north, south, west, east.
static const float kShade[8] = { 1.0, 0.5, 1.0, 0.8, 0.8, 0.6, 0.6, 1.0 };

VSOut VSMain(VSIn i)
{
	VSOut o;
	o.pos = mul(viewProj, float4(origin.xyz + i.pos, 1.0));
	o.uv = i.uv;
	o.color = i.color;
	o.light = float2(i.light & 255, (i.light >> 8) & 255) / 15.0;
	o.flags = i.flags;
	return o;
}

float4 PSMain(VSOut i) : SV_Target
{
	float4 tex = (i.flags & 4) ? float4(1, 1, 1, 1)
		: ((i.flags & 8) ? atlas.SampleLevel(pointSampler, i.uv, 0) : atlas.Sample(mipSampler, i.uv));
	float4 c = tex * i.color;
	if (mode.x < 0.5 && (i.flags & 1) && c.a < 0.1) {
		discard;
	}
	float lit = max(Curve(i.light.x), Curve(i.light.y));
	c.rgb *= kShade[(i.flags >> 4) & 7] * max(lit, 0.04);
	if (mode.x > 0.5) {
		c.rgb *= c.a;
	} else {
		c.a = 1.0;
	}
	return c;
}
)";

		template <class T>
		void Release(T*& a_ptr)
		{
			if (a_ptr) {
				a_ptr->Release();
				a_ptr = nullptr;
			}
		}

		void Log(const char* a_line)
		{
			wchar_t path[MAX_PATH]{};
			if (!::GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH)) {
				return;
			}
			::lstrcatW(path, L"\\WitchCraft\\host.log");
			HANDLE file = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) {
				return;
			}
			DWORD wrote = 0;
			::WriteFile(file, a_line, static_cast<DWORD>(std::strlen(a_line)), &wrote, nullptr);
			::WriteFile(file, "\r\n", 2, &wrote, nullptr);
			::CloseHandle(file);
		}

		struct Section
		{
			ID3D11Buffer* vb{ nullptr };
			std::uint32_t opaque{ 0 };
			std::uint32_t translucent{ 0 };
			std::int32_t  sx{ 0 };
			std::int32_t  sy{ 0 };
			std::int32_t  sz{ 0 };
		};

		struct Texture
		{
			ID3D11Texture2D*          tex{ nullptr };
			ID3D11ShaderResourceView* srv{ nullptr };
		};

		struct Mesh
		{
			std::vector<skycraft::proto::RenBatch> batches;
			ID3D11Buffer*                          vb{ nullptr };
			UINT                                   capacity{ 0 };
			double                                 origin[3]{};
		};

		struct FrameCB
		{
			float viewProj[16];
			float mode[4];
		};

		ID3D11Device*           device = nullptr;
		ID3D11DeviceContext*    context = nullptr;
		ID3D11VertexShader*     vs = nullptr;
		ID3D11PixelShader*      ps = nullptr;
		ID3D11InputLayout*      layout = nullptr;
		ID3D11Buffer*           frameCb = nullptr;
		ID3D11Buffer*           objectCb = nullptr;
		ID3D11SamplerState*     mipSampler = nullptr;
		ID3D11SamplerState*     pointSampler = nullptr;
		ID3D11DepthStencilState* depthWrite = nullptr;
		ID3D11DepthStencilState* depthRead = nullptr;
		ID3D11RasterizerState* occlusionRaster = nullptr;
		Mesh occlusion;
		std::uint64_t occlusionRevision=0;
		UINT occlusionCount=0;
		ID3D11BlendState*       blend = nullptr;
		ID3D11RasterizerState*  raster = nullptr;
		ID3D11Texture2D*        color = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;
		ID3D11Texture2D*        depth = nullptr;
		ID3D11DepthStencilView* dsv = nullptr;
		ID3D11Texture2D*        staging = nullptr;
		ID3D11Texture2D*        atlasTex = nullptr;
		ID3D11ShaderResourceView* atlasSrv = nullptr;
		ID3D11Texture2D*        whiteTex = nullptr;
		ID3D11ShaderResourceView* whiteSrv = nullptr;
		int                     targetW = 0;
		int                     targetH = 0;
		bool                    initFailed = false;
		bool                    loggedWorld = false;
		bool                    loggedAvatar = false;
		bool                    loggedAtlas = false;
		bool                    atlasMipsStale = false;
		std::unordered_map<std::uint64_t, Section> sections;
		std::unordered_map<std::uint32_t, Texture> entityTextures;
		Mesh                    avatar;
		Mesh                    scene;
		std::vector<Vertex>     scratch;

		std::uint64_t Key(std::int32_t a_x, std::int32_t a_y, std::int32_t a_z)
		{
			return (std::uint64_t(std::uint32_t(a_x) & 0x1FFFFF) << 42) | (std::uint64_t(std::uint32_t(a_y) & 0x1FFFFF) << 21) | (std::uint32_t(a_z) & 0x1FFFFF);
		}

		void ClearMesh(Mesh& a_mesh)
		{
			a_mesh.batches.clear();
		}

		void ClearSections()
		{
			for (auto& [key, section] : sections) {
				Release(section.vb);
				(void)key;
			}
			sections.clear();
			ClearMesh(avatar);
			ClearMesh(scene);
		}

		bool Compile(const char* a_entry, const char* a_target, ID3DBlob** a_out)
		{
			ID3DBlob* errors = nullptr;
			const auto hr = ::D3DCompile(kShader, sizeof(kShader) - 1, "witchcraft_world", nullptr, nullptr, a_entry, a_target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, a_out, &errors);
			if (FAILED(hr)) {
				char line[512] = "world shader failed";
				if (errors) {
					std::snprintf(line, sizeof(line), "world shader failed: %.400s", static_cast<const char*>(errors->GetBufferPointer()));
				}
				Log(line);
				Release(errors);
				return false;
			}
			Release(errors);
			return true;
		}

		bool InitDevice()
		{
			if (initFailed) {
				return false;
			}
			if (device) {
				return true;
			}
			const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
			const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
			HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &device, nullptr, &context);
			if (FAILED(hr)) {
				hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &device, nullptr, &context);
			}
			if (FAILED(hr)) {
				char line[80];
				std::snprintf(line, sizeof(line), "world device failed %08lx", static_cast<unsigned long>(hr));
				Log(line);
				initFailed = true;
				return false;
			}
			ID3DBlob* vsBlob = nullptr;
			ID3DBlob* psBlob = nullptr;
			if (!Compile("VSMain", "vs_5_0", &vsBlob) || !Compile("PSMain", "ps_5_0", &psBlob)) {
				Release(vsBlob);
				Release(psBlob);
				Release(context);
				Release(device);
				initFailed = true;
				return false;
			}
			device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
			device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);
			const D3D11_INPUT_ELEMENT_DESC elements[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 1, DXGI_FORMAT_R32_UINT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 2, DXGI_FORMAT_R32_UINT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			device->CreateInputLayout(elements, 5, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout);
			Release(vsBlob);
			Release(psBlob);

			auto constant = [](UINT a_bytes, ID3D11Buffer** a_out) {
				D3D11_BUFFER_DESC bd{};
				bd.ByteWidth = a_bytes;
				bd.Usage = D3D11_USAGE_DYNAMIC;
				bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				device->CreateBuffer(&bd, nullptr, a_out);
			};
			constant(sizeof(FrameCB), &frameCb);
			constant(16, &objectCb);

			D3D11_SAMPLER_DESC sd{};
			// Preserve Minecraft's pixel textures near the camera; blend only mip levels.
			sd.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
			sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			device->CreateSamplerState(&sd, &mipSampler);
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sd.MaxLOD = 0;
			device->CreateSamplerState(&sd, &pointSampler);

			const std::uint32_t whitePx = 0xFFFFFFFFu;
			D3D11_TEXTURE2D_DESC wtd{};
			wtd.Width = 1;
			wtd.Height = 1;
			wtd.MipLevels = 1;
			wtd.ArraySize = 1;
			wtd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			wtd.SampleDesc.Count = 1;
			wtd.Usage = D3D11_USAGE_IMMUTABLE;
			wtd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA winit{ &whitePx, 4, 0 };
			device->CreateTexture2D(&wtd, &winit, &whiteTex);
			device->CreateShaderResourceView(whiteTex, nullptr, &whiteSrv);

			D3D11_DEPTH_STENCIL_DESC dd{};
			dd.DepthEnable = TRUE;
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			dd.DepthFunc = D3D11_COMPARISON_LESS;
			device->CreateDepthStencilState(&dd, &depthWrite);
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			device->CreateDepthStencilState(&dd, &depthRead);

			D3D11_BLEND_DESC bd{};
			bd.RenderTarget[0].BlendEnable = TRUE;
			bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
			bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			device->CreateBlendState(&bd, &blend);

			D3D11_RASTERIZER_DESC rd{};
			rd.FillMode = D3D11_FILL_SOLID;
			rd.CullMode = D3D11_CULL_BACK;
			rd.FrontCounterClockwise = TRUE;
			rd.DepthClipEnable = TRUE;
			device->CreateRasterizerState(&rd, &raster);
			rd.CullMode = D3D11_CULL_NONE;
			rd.DepthBias = 2;
			device->CreateRasterizerState(&rd, &occlusionRaster);
			if (!vs || !ps || !layout || !frameCb || !objectCb) {
				Log("world pipeline incomplete");
				Release(context);
				Release(device);
				initFailed = true;
				return false;
			}
			Log("world pipeline ready");
			return true;
		}

		void ReleaseTarget()
		{
			Release(staging);
			Release(dsv);
			Release(depth);
			Release(rtv);
			Release(color);
			targetW = 0;
			targetH = 0;
		}

		bool EnsureTarget(int a_w, int a_h)
		{
			if (color && targetW == a_w && targetH == a_h) {
				return true;
			}
			ReleaseTarget();
			D3D11_TEXTURE2D_DESC td{};
			td.Width = static_cast<UINT>(a_w);
			td.Height = static_cast<UINT>(a_h);
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.BindFlags = D3D11_BIND_RENDER_TARGET;
			if (FAILED(device->CreateTexture2D(&td, nullptr, &color)) || FAILED(device->CreateRenderTargetView(color, nullptr, &rtv))) {
				ReleaseTarget();
				return false;
			}
			td.BindFlags = 0;
			td.Usage = D3D11_USAGE_STAGING;
			td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			if (FAILED(device->CreateTexture2D(&td, nullptr, &staging))) {
				ReleaseTarget();
				return false;
			}
			D3D11_TEXTURE2D_DESC dd = td;
			dd.Usage = D3D11_USAGE_DEFAULT;
			dd.CPUAccessFlags = 0;
			dd.Format = DXGI_FORMAT_D32_FLOAT;
			dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			if (FAILED(device->CreateTexture2D(&dd, nullptr, &depth)) || FAILED(device->CreateDepthStencilView(depth, nullptr, &dsv))) {
				ReleaseTarget();
				return false;
			}
			targetW = a_w;
			targetH = a_h;
			return true;
		}

		void OnAtlas(const std::uint8_t* a_data, std::uint32_t a_bytes)
		{
			if (a_bytes < sizeof(skycraft::proto::RenAtlas)) {
				return;
			}
			const auto* hdr = reinterpret_cast<const skycraft::proto::RenAtlas*>(a_data);
			if (!hdr->width || !hdr->height || hdr->width > 8192 || hdr->height > 8192 ||
				a_bytes < sizeof(skycraft::proto::RenAtlas) + std::uint64_t(hdr->width) * hdr->height * 4) {
				return;
			}
			Release(atlasSrv);
			Release(atlasTex);
			D3D11_TEXTURE2D_DESC td{};
			td.Width = hdr->width;
			td.Height = hdr->height;
			td.MipLevels = 5;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
			if (FAILED(device->CreateTexture2D(&td, nullptr, &atlasTex)) || FAILED(device->CreateShaderResourceView(atlasTex, nullptr, &atlasSrv))) {
				Release(atlasTex);
				return;
			}
			context->UpdateSubresource(atlasTex, 0, nullptr, a_data + sizeof(skycraft::proto::RenAtlas), hdr->width * 4, 0);
			context->GenerateMips(atlasSrv);
			if (!loggedAtlas) {
				loggedAtlas = true;
				char line[80];
				std::snprintf(line, sizeof(line), "block atlas %u x %u", hdr->width, hdr->height);
				Log(line);
			}
		}

		void OnAtlasRegion(const std::uint8_t* a_data, std::uint32_t a_bytes)
		{
			if (!atlasTex || a_bytes < sizeof(skycraft::proto::RenAtlasRegion)) {
				return;
			}
			const auto* hdr = reinterpret_cast<const skycraft::proto::RenAtlasRegion*>(a_data);
			D3D11_TEXTURE2D_DESC td{};
			atlasTex->GetDesc(&td);
			if (!hdr->width || !hdr->height || hdr->x + hdr->width > td.Width || hdr->y + hdr->height > td.Height ||
				a_bytes < sizeof(skycraft::proto::RenAtlasRegion) + std::uint64_t(hdr->width) * hdr->height * 4) {
				return;
			}
			const D3D11_BOX box{ hdr->x, hdr->y, 0, hdr->x + hdr->width, hdr->y + hdr->height, 1 };
			context->UpdateSubresource(atlasTex, 0, &box, a_data + sizeof(skycraft::proto::RenAtlasRegion), hdr->width * 4, 0);
			atlasMipsStale = true;
		}

		void OnTexture(const std::uint8_t* a_data, std::uint32_t a_bytes)
		{
			if (a_bytes < sizeof(skycraft::proto::RenTexture)) {
				return;
			}
			const auto* hdr = reinterpret_cast<const skycraft::proto::RenTexture*>(a_data);
			if (!hdr->id || !hdr->width || !hdr->height || hdr->width > 4096 || hdr->height > 4096 ||
				a_bytes < sizeof(skycraft::proto::RenTexture) + std::uint64_t(hdr->width) * hdr->height * 4) {
				return;
			}
			auto& t = entityTextures[hdr->id];
			Release(t.srv);
			Release(t.tex);
			D3D11_TEXTURE2D_DESC td{};
			td.Width = hdr->width;
			td.Height = hdr->height;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_IMMUTABLE;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA init{ a_data + sizeof(skycraft::proto::RenTexture), hdr->width * 4, 0 };
			if (FAILED(device->CreateTexture2D(&td, &init, &t.tex)) || FAILED(device->CreateShaderResourceView(t.tex, nullptr, &t.srv))) {
				Release(t.tex);
				entityTextures.erase(hdr->id);
			}
		}

		void OnSection(const std::uint8_t* a_data, std::uint32_t a_bytes)
		{
			if (a_bytes < sizeof(skycraft::proto::RenSection)) {
				return;
			}
			const auto* hdr = reinterpret_cast<const skycraft::proto::RenSection*>(a_data);
			const auto  key = Key(hdr->sx, hdr->sy, hdr->sz);
			if (auto it = sections.find(key); it != sections.end()) {
				Release(it->second.vb);
				sections.erase(it);
			}
			const std::uint32_t count = hdr->vertexCount - hdr->vertexCount % 3;
			if (count == 0 || count > 2000000 || a_bytes < sizeof(skycraft::proto::RenSection) + std::uint64_t(count) * sizeof(Vertex)) {
				return;
			}
			const auto* src = reinterpret_cast<const Vertex*>(a_data + sizeof(skycraft::proto::RenSection));
			scratch.clear();
			scratch.reserve(count);
			for (int pass = 0; pass < 2; ++pass) {
				for (std::uint32_t t = 0; t < count; t += 3) {
					const bool translucent = (src[t].flags & kFlagTranslucent) != 0;
					if (translucent == (pass == 1)) {
						scratch.insert(scratch.end(), src + t, src + t + 3);
					}
				}
				if (pass == 0) {
					sections[key].opaque = static_cast<std::uint32_t>(scratch.size());
				}
			}
			Section& s = sections[key];
			s.translucent = static_cast<std::uint32_t>(scratch.size()) - s.opaque;
			s.sx = hdr->sx;
			s.sy = hdr->sy;
			s.sz = hdr->sz;
			D3D11_BUFFER_DESC bd{};
			bd.ByteWidth = static_cast<UINT>(scratch.size() * sizeof(Vertex));
			bd.Usage = D3D11_USAGE_IMMUTABLE;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA init{ scratch.data(), 0, 0 };
			if (FAILED(device->CreateBuffer(&bd, &init, &s.vb))) {
				sections.erase(key);
			}
		}

		void OnMesh(Mesh& a_mesh, const std::uint8_t* a_data, std::uint32_t a_bytes, bool a_hasOrigin)
		{
			a_mesh.batches.clear();
			const std::size_t head = a_hasOrigin ? sizeof(skycraft::proto::RenScene) : sizeof(skycraft::proto::RenAvatar);
			if (a_bytes < head) {
				return;
			}
			std::uint32_t batchCount = 0;
			std::uint32_t vertexCount = 0;
			if (a_hasOrigin) {
				const auto* hdr = reinterpret_cast<const skycraft::proto::RenScene*>(a_data);
				a_mesh.origin[0] = hdr->originX;
				a_mesh.origin[1] = hdr->originY;
				a_mesh.origin[2] = hdr->originZ;
				batchCount = hdr->batchCount;
				vertexCount = hdr->vertexCount;
			} else {
				const auto* hdr = reinterpret_cast<const skycraft::proto::RenAvatar*>(a_data);
				batchCount = hdr->batchCount;
				vertexCount = hdr->vertexCount;
			}
			if (batchCount == 0 || vertexCount == 0 || batchCount > 100000 || vertexCount > 2000000) {
				return;
			}
			const std::uint64_t need = head + std::uint64_t(batchCount) * sizeof(skycraft::proto::RenBatch) + std::uint64_t(vertexCount) * sizeof(Vertex);
			if (a_bytes < need) {
				return;
			}
			const auto* batches = reinterpret_cast<const skycraft::proto::RenBatch*>(a_data + head);
			const auto* verts = reinterpret_cast<const Vertex*>(batches + batchCount);
			const UINT bytes = vertexCount * sizeof(Vertex);
			if (bytes > a_mesh.capacity) {
				Release(a_mesh.vb);
				D3D11_BUFFER_DESC bd{};
				bd.ByteWidth = bytes < 256 * 1024 ? 256 * 1024 : bytes * 2;
				bd.Usage = D3D11_USAGE_DYNAMIC;
				bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				if (FAILED(device->CreateBuffer(&bd, nullptr, &a_mesh.vb))) {
					a_mesh.capacity = 0;
					return;
				}
				a_mesh.capacity = bd.ByteWidth;
			}
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(a_mesh.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				return;
			}
			std::memcpy(mapped.pData, verts, bytes);
			context->Unmap(a_mesh.vb, 0);
			for (std::uint32_t b = 0; b < batchCount; ++b) {
				if (batches[b].first + batches[b].count <= vertexCount) {
					a_mesh.batches.push_back(batches[b]);
				}
			}
		}

		void Drain()
		{
			Link::Get().DrainRender(
				[](std::uint32_t a_type, const std::uint8_t* a_data, std::uint32_t a_bytes) {
					switch (a_type) {
					case skycraft::proto::kRenAtlas:
						OnAtlas(a_data, a_bytes);
						break;
					case skycraft::proto::kRenAtlasRegion:
						OnAtlasRegion(a_data, a_bytes);
						break;
					case skycraft::proto::kRenSection:
						OnSection(a_data, a_bytes);
						break;
					case skycraft::proto::kRenClearAll:
						ClearSections();
						break;
					case skycraft::proto::kRenTexture:
						OnTexture(a_data, a_bytes);
						break;
					case skycraft::proto::kRenAvatar:
						OnMesh(avatar, a_data, a_bytes, false);
						break;
					case skycraft::proto::kRenScene:
						OnMesh(scene, a_data, a_bytes, true);
						break;
					default:
						break;
					}
				},
				48ull << 20);
			if (std::exchange(atlasMipsStale, false) && atlasSrv) {
				context->GenerateMips(atlasSrv);
			}
		}

		void Mul(const float a[16], const float b[16], float o[16])
		{
			for (int row = 0; row < 4; ++row) {
				for (int col = 0; col < 4; ++col) {
					o[row * 4 + col] = a[row * 4 + 0] * b[0 * 4 + col] + a[row * 4 + 1] * b[1 * 4 + col] + a[row * 4 + 2] * b[2 * 4 + col] + a[row * 4 + 3] * b[3 * 4 + col];
				}
			}
		}

		struct Camera
		{
			double x, y, z;
			float  fx, fy, fz;
			float  rx, ry, rz;
			float  ux, uy, uz;
			float  viewProj[16];
		};

		// Minecraft's camera: yaw 0 looks +Z, positive yaw turns toward -X, pitch positive looks down.
		bool MakeCamera(const skycraft::proto::McState& a_mc, int a_w, int a_h, Camera& a_out, const GameFrame* a_game)
		{
			float yaw = (a_game ? std::atan2(-a_game->forward[0], a_game->forward[2]) : a_mc.yaw * kPi / 180.0f);
			float pitch = (a_game ? std::atan2(-a_game->forward[1], std::hypot(a_game->forward[0], a_game->forward[2])) : a_mc.pitch * kPi / 180.0f);
			if ((!a_game || (a_game->flags & kGameFrontView)) && a_mc.cameraMode == 2) {
				yaw += kPi;
				pitch = -pitch;
			}
			const float cp = std::cos(pitch);
			const float sp = std::sin(pitch);
			const float cy = std::cos(yaw);
			const float sy = std::sin(yaw);
			const float fx = -sy * cp;
			const float fy = -sp;
			const float fz = cy * cp;
			double ex = a_mc.eyeX;
			double ey = a_mc.eyeY;
			double ez = a_mc.eyeZ;
			if (ex == 0.0 && ey == 0.0 && ez == 0.0) {
				ex = a_mc.x;
				ey = a_mc.y + a_mc.eyeHeight;
				ez = a_mc.z;
			}
			float dist = 0.0f;
			if (a_mc.cameraMode != 0) {
				dist = (a_mc.cameraDistance > 0.2f && a_mc.cameraDistance < 64.0f) ? a_mc.cameraDistance : 4.0f;
			}
			a_out.x = a_game ? a_game->camera[0] : ex - static_cast<double>(fx) * dist;
			a_out.y = a_game ? a_game->camera[1] : ey - static_cast<double>(fy) * dist;
			a_out.z = a_game ? a_game->camera[2] : ez - static_cast<double>(fz) * dist;
			a_out.fx = fx;
			a_out.fy = fy;
			a_out.fz = fz;

			float rx = -fz;
			float rz = fx;
			float rl = std::sqrt(rx * rx + rz * rz);
			if (rl < 1.0e-4f) {
				rx = cy;
				rz = sy;
				rl = 1.0f;
			}
			rx /= rl;
			rz /= rl;
			const float ry = 0.0f;
			const float ux = ry * fz - rz * fy;
			const float uy = rz * fx - rx * fz;
			const float uz = rx * fy - ry * fx;
			a_out.rx = rx;
			a_out.ry = ry;
			a_out.rz = rz;
			a_out.ux = ux;
			a_out.uy = uy;
			a_out.uz = uz;
			const float view[16] = {
				rx, ry, rz, 0,
				ux, uy, uz, 0,
				-fx, -fy, -fz, 0,
				0, 0, 0, 1
			};
			float fov = a_game ? a_game->verticalFov : a_mc.fovDeg;
			if (!(fov > 20.0f && fov < 140.0f)) {
				fov = 70.0f;
			}
			const float ys = 1.0f / std::tan(fov * kPi / 180.0f * 0.5f);
			const float xs = ys * static_cast<float>(a_h) / static_cast<float>(a_w);
			constexpr float n = 0.05f;
			constexpr float f = 512.0f;
			const float proj[16] = {
				xs, 0, 0, 0,
				0, ys, 0, 0,
				0, 0, f / (n - f), n * f / (n - f),
				0, 0, -1, 0
			};
			Mul(proj, view, a_out.viewProj);
			return true;
		}

		void SetOffset(const Camera& a_cam, double a_x, double a_y, double a_z)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(objectCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				return;
			}
			auto* o = static_cast<float*>(mapped.pData);
			o[0] = static_cast<float>(a_x - a_cam.x);
			o[1] = static_cast<float>(a_y - a_cam.y);
			o[2] = static_cast<float>(a_z - a_cam.z);
			o[3] = 0.0f;
			context->Unmap(objectCb, 0);
		}

		void DrawMesh(const Camera& a_cam, const Mesh& a_mesh, double a_x, double a_y, double a_z, bool a_blended)
		{
			if (a_mesh.batches.empty() || !a_mesh.vb) {
				return;
			}
			SetOffset(a_cam, a_x, a_y, a_z);
			const UINT stride = sizeof(Vertex);
			const UINT zero = 0;
			context->IASetVertexBuffers(0, 1, &a_mesh.vb, &stride, &zero);
			for (const auto& batch : a_mesh.batches) {
				if (((batch.flags & 1) != 0) != a_blended || batch.count == 0) {
					continue;
				}
				ID3D11ShaderResourceView* srv = atlasSrv;
				if (batch.texture != 0) {
					const auto it = entityTextures.find(batch.texture);
					srv = (it == entityTextures.end() || !it->second.srv) ? (whiteSrv ? whiteSrv : atlasSrv) : it->second.srv;
				}
				context->PSSetShaderResources(0, 1, &srv);
				context->Draw(batch.count, batch.first);
			}
			context->PSSetShaderResources(0, 1, &atlasSrv);
		}

		void DrawPass(const Camera& a_cam, const skycraft::proto::McState& a_mc, bool a_blended,bool avatarOnly)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				return;
			}
			auto* frame = static_cast<FrameCB*>(mapped.pData);
			std::memcpy(frame->viewProj, a_cam.viewProj, sizeof(frame->viewProj));
			frame->mode[0] = a_blended ? 1.0f : 0.0f;
			frame->mode[1] = frame->mode[2] = frame->mode[3] = 0.0f;
			context->Unmap(frameCb, 0);
			context->OMSetBlendState(a_blended ? blend : nullptr, nullptr, 0xFFFFFFFF);
			context->OMSetDepthStencilState(a_blended ? depthRead : depthWrite, 0);
			const UINT stride = sizeof(Vertex);
			const UINT zero = 0;
			for (const auto& [key, section] : sections) {
				if(avatarOnly) break;
				(void)key;
				const UINT count = a_blended ? section.translucent : section.opaque;
				const UINT start = a_blended ? section.opaque : 0;
				if (!section.vb || count == 0) {
					continue;
				}
				const double dx = section.sx * 16.0 + 8.0 - a_cam.x;
				const double dy = section.sy * 16.0 + 8.0 - a_cam.y;
				const double dz = section.sz * 16.0 + 8.0 - a_cam.z;
				if (dx * dx + dy * dy + dz * dz > 96.0 * 96.0) {
					continue;
				}
				SetOffset(a_cam, section.sx * 16.0, section.sy * 16.0, section.sz * 16.0);
				context->IASetVertexBuffers(0, 1, &section.vb, &stride, &zero);
				context->PSSetShaderResources(0, 1, &atlasSrv);
				context->Draw(count, start);
			}
			// The avatar shares the block world position. Camera framing belongs to Witcher.
			DrawMesh(a_cam, avatar, a_mc.x, a_mc.y, a_mc.z, a_blended);
			if(!avatarOnly) DrawMesh(a_cam, scene, scene.origin[0], scene.origin[1], scene.origin[2], a_blended);
		}

		void DrawOcclusion(const Camera& camera,const GameFrame* game) {
			OcclusionGeometry geometry;
			if(!game||!ReadOcclusionGeometry(*game,occlusionRevision,geometry)) {
				Release(occlusion.vb);occlusionRevision=0;occlusionCount=0;return;
			}
			if(geometry.revision&&geometry.revision!=occlusionRevision) {
				const auto vertices=OcclusionVertices(geometry.triangles,occlusion.origin);
				ID3D11Buffer* buffer=nullptr;
				if(!vertices.empty()) {
					D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(vertices.size()*sizeof(Vertex));
					desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
					D3D11_SUBRESOURCE_DATA initial{vertices.data(),0,0};
					if(FAILED(device->CreateBuffer(&desc,&initial,&buffer))) return;
				}
				Release(occlusion.vb);occlusion.vb=buffer;occlusionCount=static_cast<UINT>(vertices.size());
				occlusionRevision=geometry.revision;
				static ULONGLONG lastDepthLog=0;
				if(!lastDepthLog||GetTickCount64()-lastDepthLog>=5000) {
					char line[128]{};std::snprintf(line,sizeof(line),"occlusion depth: triangles=%u revision=%llu",
						occlusionCount/3,static_cast<unsigned long long>(occlusionRevision));Log(line);lastDepthLog=GetTickCount64();
				}
			}
			if(!occlusion.vb||!occlusionCount||!occlusionRaster) return;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if(FAILED(context->Map(frameCb,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return;
			auto* frame=static_cast<FrameCB*>(mapped.pData);
			std::memcpy(frame->viewProj,camera.viewProj,sizeof(frame->viewProj));
			std::memset(frame->mode,0,sizeof(frame->mode));context->Unmap(frameCb,0);
			SetOffset(camera,occlusion.origin[0],occlusion.origin[1],occlusion.origin[2]);
			// No color target: Witcher surfaces contribute depth while its image
			// stays visible through untouched transparent overlay pixels.
			context->OMSetRenderTargets(0,nullptr,dsv);
			context->OMSetDepthStencilState(depthWrite,0);
			context->RSSetState(occlusionRaster);context->PSSetShader(nullptr,nullptr,0);
			const UINT stride=sizeof(Vertex),zero=0;
			context->IASetVertexBuffers(0,1,&occlusion.vb,&stride,&zero);context->Draw(occlusionCount,0);
			context->OMSetRenderTargets(1,&rtv,dsv);
			context->RSSetState(raster);context->PSSetShader(ps,nullptr,0);
		}

		bool Present(std::uint8_t* a_bgra, int a_w, int a_h, const skycraft::proto::McState& a_mc, const GameFrame* a_game,bool avatarOnly)
		{
			if (!atlasSrv || !EnsureTarget(a_w, a_h)) {
				return false;
			}
			Camera cam{};
			MakeCamera(a_mc, a_w, a_h, cam, a_game);
			// Empty pixels stay transparent so the Witcher world shows through. Steve and blocks
			// are the only opaque pixels, drawn where Minecraft's third-person camera sees them.
			const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			context->OMSetRenderTargets(1, &rtv, dsv);
			context->ClearRenderTargetView(rtv, clear);
			context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(a_w);
			viewport.Height = static_cast<float>(a_h);
			viewport.MaxDepth = 1.0f;
			context->RSSetViewports(1, &viewport);
			context->RSSetState(raster);
			context->IASetInputLayout(layout);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vs, nullptr, 0);
			context->PSSetShader(ps, nullptr, 0);
			context->VSSetConstantBuffers(0, 1, &frameCb);
			context->VSSetConstantBuffers(1, 1, &objectCb);
			context->PSSetConstantBuffers(0, 1, &frameCb);
			ID3D11SamplerState* samplers[] = { mipSampler, pointSampler };
			context->PSSetSamplers(0, 2, samplers);
			if(!avatarOnly) DrawOcclusion(cam,a_game);
			DrawPass(cam, a_mc, false,avatarOnly);
			DrawPass(cam, a_mc, true,avatarOnly);
			context->OMSetRenderTargets(0, nullptr, nullptr);
			LARGE_INTEGER readbackBegin{},mappedAt{},copiedAt{},frequency{};
			QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&readbackBegin);
			context->CopyResource(staging, color);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
				return false;
			}
			QueryPerformanceCounter(&mappedAt);
			const auto row = static_cast<std::size_t>(a_w) * 4;
			for (int y = 0; y < a_h; ++y) {
				std::memcpy(a_bgra + row * y, static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(mapped.RowPitch) * y, row);
			}
			context->Unmap(staging, 0);
			QueryPerformanceCounter(&copiedAt);
			static double waitMs=0,copyMs=0;static unsigned readbacks=0;
			static ULONGLONG timingAt=GetTickCount64();
			waitMs+=(mappedAt.QuadPart-readbackBegin.QuadPart)*1000.0/frequency.QuadPart;
			copyMs+=(copiedAt.QuadPart-mappedAt.QuadPart)*1000.0/frequency.QuadPart;++readbacks;
			if(GetTickCount64()-timingAt>=5000) {
				char timing[160]{};std::snprintf(timing,sizeof(timing),"world readback: GPU wait=%.2fms bitmap copy=%.2fms frames=%u",waitMs/readbacks,copyMs/readbacks,readbacks);
				Log(timing);waitMs=copyMs=0;readbacks=0;timingAt=GetTickCount64();
			}
			if (!loggedAvatar && !avatar.batches.empty()) {
				loggedAvatar = true;
				char line[160];
				std::snprintf(line, sizeof(line), "steve drawn, %zu batches, camera mode %u distance %.2f", avatar.batches.size(), a_mc.cameraMode, a_mc.cameraDistance);
				Log(line);
			}
			if (!loggedWorld && (!sections.empty() || !avatar.batches.empty() || !scene.batches.empty())) {
				loggedWorld = true;
				char line[160];
				std::snprintf(line, sizeof(line), "world on screen, %zu sections, camera %.1f %.1f %.1f", sections.size(), cam.x, cam.y, cam.z);
				Log(line);
			}
			return true;
		}
	}

	void PumpRender()
	{
		if (!InitDevice()) {
			Link::Get().DrainRender([](std::uint32_t, const std::uint8_t*, std::uint32_t) {}, 48ull << 20);
			return;
		}
		Drain();
	}

	bool DrawWorld(std::uint8_t* a_bgra, int a_width, int a_height, const skycraft::proto::McState& a_mc, const GameFrame* a_game,bool avatarOnly)
	{
		if (!a_bgra || a_width < 16 || a_height < 16) {
			return false;
		}
		if (!InitDevice()) {
			Link::Get().DrainRender([](std::uint32_t, const std::uint8_t*, std::uint32_t) {}, 48ull << 20);
			return false;
		}
		Drain();
		if ((a_mc.flags & skycraft::proto::kMcInWorld) == 0) {
			static bool logged = false;
			if (!logged && !sections.empty()) {
				logged = true;
				Log("block meshes are in, waiting until Minecraft is in a world");
			}
			return false;
		}
        LARGE_INTEGER now{},frequency{};
        QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
        const auto renderMc=witchcraft::RenderMotionState(a_mc,now.QuadPart,frequency.QuadPart);
        return Present(a_bgra, a_width, a_height, renderMc, a_game,avatarOnly);
	}
}

