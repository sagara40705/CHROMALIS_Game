#include "cooker/CookStatic.h"
#include <windows.h>
#include <wincodec.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <cgltf.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>

namespace chromalis {
namespace {
using Microsoft::WRL::ComPtr;
using namespace DirectX;
void Need(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
void Hr(HRESULT hr, const char* context) { Need(SUCCEEDED(hr), std::format("{}: HRESULT 0x{:08x}", context, static_cast<unsigned>(hr))); }
std::string Hash(std::span<const uint8_t> bytes) {
    std::array<uint8_t, 32> digest{};
    Need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size())) >= 0, "SHA256 failed");
    std::string text; for (auto b : digest) text += std::format("{:02x}", b); return text;
}
std::vector<uint8_t> DecodeDataUri(const std::string& uri) {
    const auto comma = uri.find(',');
    Need(comma != std::string::npos && uri.substr(0, comma).ends_with(";base64"), "Only base64 data URIs are supported");
    const auto payload = uri.substr(comma + 1);
    DWORD size = 0;
    Need(CryptStringToBinaryA(payload.c_str(), static_cast<DWORD>(payload.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size, nullptr, nullptr) != 0, "Invalid base64 data URI");
    Need(size && size <= 512u*1024*1024, "Data URI exceeds limit");
    std::vector<uint8_t> bytes(size);
    Need(CryptStringToBinaryA(payload.c_str(), static_cast<DWORD>(payload.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, bytes.data(), &size, nullptr, nullptr) != 0, "Cannot decode base64");
    return bytes;
}
struct Import {
    std::filesystem::path root;
    StaticAsset asset;
    std::vector<uint8_t> External(const char* raw) {
        Need(raw != nullptr, "Missing external URI");
        std::string uri(raw);
        if (uri.starts_with("data:")) return DecodeDataUri(uri);
        Need(uri.find_first_of("?#") == std::string::npos, "URI query/fragment unsupported");
        const auto len = cgltf_decode_uri(uri.data()); uri.resize(len);
        Need(uri.find('\0') == std::string::npos && uri.find(':') == std::string::npos && uri.find('\\') == std::string::npos, "Only relative local asset URIs are supported");
        const auto relative = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(uri.data()),uri.size()));
        Need(!relative.is_absolute(), "Absolute asset URI is unsupported");
        const auto path = std::filesystem::weakly_canonical(root / relative);
        const auto local = path.lexically_relative(root);
        Need(!local.empty() && *local.begin() != L"..", "Asset URI escapes source directory");
        auto bytes = ReadAssetBytes(path);
        const auto utf8 = local.generic_u8string();
        const std::string label(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        if (std::none_of(asset.dependencies.begin(), asset.dependencies.end(), [&](const auto& d) { return d.relativePath == label; }))
            asset.dependencies.push_back({label, Hash(bytes)});
        return bytes;
    }
};
MeshTexture DecodeImage(IWICImagingFactory* factory, std::span<const uint8_t> bytes) {
    Need(!bytes.empty() && bytes.size() <= 512u*1024*1024, "Invalid image payload");
    ComPtr<IWICStream> stream; Hr(factory->CreateStream(&stream), "Create WIC stream");
    Hr(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())), "WIC image memory");
    ComPtr<IWICBitmapDecoder> decoder;
    Hr(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "Decode image (PNG/JPEG required)");
    GUID container{}; Hr(decoder->GetContainerFormat(&container), "Image container");
    Need(container == GUID_ContainerFormatPng || container == GUID_ContainerFormatJpeg, "Only PNG/JPEG source images are supported");
    ComPtr<IWICBitmapFrameDecode> frame; Hr(decoder->GetFrame(0, &frame), "Decode image frame");
    MeshTexture t; Hr(frame->GetSize(&t.width, &t.height), "Image dimensions");
    Need(t.width && t.height && t.width <= 8192 && t.height <= 8192 && uint64_t(t.width)*t.height <= 16777216, "Image exceeds 8192/16M-pixel limit");
    ComPtr<IWICFormatConverter> converter; Hr(factory->CreateFormatConverter(&converter), "Create RGBA converter");
    Hr(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert image to straight RGBA8");
    t.rgba.resize(static_cast<size_t>(t.width)*t.height*4);
    Hr(converter->CopyPixels(nullptr, t.width*4, static_cast<UINT>(t.rgba.size()), t.rgba.data()), "Copy decoded pixels");
    return t;
}
uint32_t Wrap(cgltf_wrap_mode mode) {
    if (mode == cgltf_wrap_mode_repeat) return 0;
    if (mode == cgltf_wrap_mode_clamp_to_edge) return 1;
    if (mode == cgltf_wrap_mode_mirrored_repeat) return 2;
    throw std::runtime_error("Unknown texture wrap mode");
}
std::vector<float> Attribute(const cgltf_primitive& p, cgltf_attribute_type type, cgltf_type shape, size_t count, bool required, int set = 0) {
    for (size_t i = 0; i < p.attributes_count; ++i) {
        const auto& a = p.attributes[i];
        if (a.type != type || a.index != set) continue;
        Need(a.data && a.data->type == shape && a.data->count == count, "Vertex attribute count/type mismatch");
        const size_t n = count * cgltf_num_components(shape);
        std::vector<float> out(n);
        Need(cgltf_accessor_unpack_floats(a.data, out.data(), n) == n, "Cannot unpack vertex accessor");
        for (auto f : out) Need(std::isfinite(f), "Nonfinite vertex attribute");
        return out;
    }
    Need(!required, "Required POSITION or TEXCOORD_0 attribute missing"); return {};
}
}
StaticAsset CookStatic(const std::filesystem::path& source, const std::string& id) {
    const auto absolute = std::filesystem::canonical(source);
    auto extension = absolute.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    Need(extension == L".gltf" || extension == L".glb", "Source extension must be .gltf or .glb");
    Import imp{absolute.parent_path(), {}}; imp.asset.id = id;
    const auto bytes = ReadAssetBytes(absolute);
    const auto name = absolute.filename().u8string();
    imp.asset.dependencies.push_back({std::string(reinterpret_cast<const char*>(name.data()), name.size()), Hash(bytes)});
    const std::string recipe = "CHRMESH1/cgltf1.15/WIC-RGBA8/static-LH/no-mips";
    imp.asset.dependencies.push_back({"@recipe", Hash({reinterpret_cast<const uint8_t*>(recipe.data()), recipe.size()})});
    cgltf_options options{}; cgltf_data* raw = nullptr;
    const auto parsed = cgltf_parse(&options, bytes.data(), bytes.size(), &raw);
    Need(parsed == cgltf_result_success, std::format("glTF parse failed (cgltf result {})", static_cast<int>(parsed)));
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    Need(data->nodes_count <= 65536 && data->materials_count <= 1023 && data->images_count <= 255 && data->buffers_count <= 1024, "glTF collection exceeds import limits");
    for (size_t i = 0; i < data->extensions_required_count; ++i)
        Need(std::string_view(data->extensions_required[i]) == "KHR_materials_unlit", "Unsupported required glTF extension: " + std::string(data->extensions_required[i]));
    Need(!data->skins_count && !data->animations_count, "Skins/animations are outside Phase 0B; export a static model");
    std::vector<std::vector<uint8_t>> buffers(data->buffers_count);
    uint64_t loadedBytes = bytes.size();
    for (size_t i = 0; i < data->buffers_count; ++i) {
        auto& b = data->buffers[i];
        if (b.uri) { buffers[i] = imp.External(b.uri); Need(buffers[i].size() >= b.size, "External buffer shorter than byteLength"); b.data = buffers[i].data(); loadedBytes += buffers[i].size(); }
        else { Need(i == 0 && data->bin && data->bin_size >= b.size, "Missing GLB BIN payload"); b.data = const_cast<void*>(data->bin); }
        Need(loadedBytes <= 512u*1024*1024, "Combined source buffers exceed 512 MiB");
    }
    Need(cgltf_validate(data.get()) == cgltf_result_success, "glTF validation failed: accessor/buffer/node reference or size is invalid");
    const auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Hr(init, "Initialize WIC COM");
    struct Uninit { ~Uninit() { CoUninitialize(); } } uninit;
    ComPtr<IWICImagingFactory> factory;
    Hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "Create WIC factory");
    imp.asset.textures.push_back({1, 1, {255,255,255,255}});
    uint64_t decodedBytes = 4;
    for (size_t i = 0; i < data->images_count; ++i) {
        const auto& image = data->images[i]; std::vector<uint8_t> imageBytes;
        if (image.uri) imageBytes = imp.External(image.uri);
        else {
            Need(image.buffer_view != nullptr, "Image has no URI/bufferView");
            const auto* ptr = cgltf_buffer_view_data(image.buffer_view);
            Need(ptr != nullptr, "Image bufferView has no data");
            imageBytes.assign(ptr, ptr + image.buffer_view->size);
        }
        auto texture = DecodeImage(factory.Get(), imageBytes);
        decodedBytes += texture.rgba.size(); Need(decodedBytes <= 256u*1024*1024, "Decoded texture budget exceeds 256 MiB");
        imp.asset.textures.push_back(std::move(texture));
    }
    imp.asset.materials.emplace_back();
    for (size_t i = 0; i < data->materials_count; ++i) {
        const auto& m = data->materials[i]; MeshMaterial out;
        out.unlit = m.unlit ? 1u : 0u;
        Need(m.alpha_mode != cgltf_alpha_mode_blend, "BLEND materials unsupported in 0B; use OPAQUE or MASK");
        Need(!m.has_pbr_specular_glossiness, "Specular/glossiness material extension unsupported");
        const auto& pbr = m.pbr_metallic_roughness;
        std::copy_n(pbr.base_color_factor, 4, out.baseColor.begin());
        out.metallic = pbr.metallic_factor; out.roughness = pbr.roughness_factor;
        out.doubleSided = m.double_sided ? 1u : 0u; out.alphaMask = m.alpha_mode == cgltf_alpha_mode_mask ? 1u : 0u; out.cutoff = m.alpha_cutoff;
        const auto& view = pbr.base_color_texture;
        Need(!view.has_transform && view.texcoord == 0, "Only base-color TEXCOORD_0 without texture-transform is supported");
        if (view.texture) {
            Need(view.texture->image != nullptr, "Base-color texture lacks supported image");
            out.texture = static_cast<uint32_t>(view.texture->image - data->images) + 1;
            if (auto* s = view.texture->sampler) {
                out.wrapU = Wrap(s->wrap_s); out.wrapV = Wrap(s->wrap_t);
                out.nearest = s->mag_filter == cgltf_filter_type_nearest ? 1u : 0u;
            }
        }
        imp.asset.materials.push_back(out);
    }
    std::vector<bool> visited(data->nodes_count, false);
    std::function<void(const cgltf_node*, FXMMATRIX, size_t)> visit;
    visit = [&](const cgltf_node* node, FXMMATRIX parent, size_t depth) {
        Need(depth < 128, "Scene hierarchy too deep/cyclic");
        const auto nodeIndex = static_cast<size_t>(node - data->nodes);
        Need(nodeIndex < visited.size() && !visited[nodeIndex], "Scene node cycle or duplicate reference"); visited[nodeIndex] = true;
        float matrix[16]; cgltf_node_transform_local(node, matrix);
        for (float f : matrix) Need(std::isfinite(f), "Nonfinite node transform");
        Need(matrix[3] == 0 && matrix[7] == 0 && matrix[11] == 0 && matrix[15] == 1, "Node transform must be affine");
        // glTF column-major / column vectors has the same memory layout as its row-vector transpose.
        XMFLOAT4X4 local; std::memcpy(&local, matrix, sizeof(local));
        const auto world = XMLoadFloat4x4(&local) * parent;
        const float determinant = XMVectorGetX(XMMatrixDeterminant(world));
        Need(std::isfinite(determinant) && std::abs(determinant) > 1e-8f, "Singular node transform");
        const auto normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
        if (node->mesh) for (size_t pIndex = 0; pIndex < node->mesh->primitives_count; ++pIndex) {
            const auto& p = node->mesh->primitives[pIndex];
            Need(p.type == cgltf_primitive_type_triangles && !p.targets_count && !p.has_draco_mesh_compression, "Only non-morphed, uncompressed TRIANGLES supported");
            const cgltf_accessor* pos = nullptr;
            for (size_t a = 0; a < p.attributes_count; ++a) if (p.attributes[a].type == cgltf_attribute_type_position) pos = p.attributes[a].data;
            Need(pos && pos->count && pos->count <= 2000000 - imp.asset.vertices.size(), "Missing POSITION or vertex limit exceeded");
            const size_t count = pos->count;
            auto positions = Attribute(p, cgltf_attribute_type_position, cgltf_type_vec3, count, true);
            auto normals = Attribute(p, cgltf_attribute_type_normal, cgltf_type_vec3, count, false);
            const auto material = p.material ? static_cast<uint32_t>(p.material - data->materials) + 1 : 0;
            auto uvs = Attribute(p, cgltf_attribute_type_texcoord, cgltf_type_vec2, count, imp.asset.materials[material].texture != 0);
            const auto indexCount = p.indices ? p.indices->count : count;
            Need(indexCount && indexCount % 3 == 0 && indexCount <= 6000000 - imp.asset.indices.size(), "Invalid triangle index count");
            std::vector<uint32_t> indices(indexCount);
            if (p.indices) {
                Need(!p.indices->is_sparse && p.indices->type == cgltf_type_scalar &&
                    (p.indices->component_type == cgltf_component_type_r_8u || p.indices->component_type == cgltf_component_type_r_16u || p.indices->component_type == cgltf_component_type_r_32u), "Unsupported index accessor");
                Need(cgltf_accessor_unpack_indices(p.indices, indices.data(), sizeof(uint32_t), indices.size()) == indices.size(), "Cannot unpack indices");
            } else for (uint32_t n = 0; n < indices.size(); ++n) indices[n] = n;
            for (auto n : indices) Need(n < count, "Index exceeds vertex count");
            if (normals.empty()) {
                // glTF missing normals means flat shading: split corners instead of averaging shared vertices.
                std::vector<float> flatPos, flatNormal, flatUv;
                for (size_t n = 0; n < indices.size(); n += 3) {
                    const auto load = [&](uint32_t index) { return XMVectorSet(positions[index*3], positions[index*3+1], positions[index*3+2], 0); };
                    auto normal = XMVector3Cross(load(indices[n+1])-load(indices[n]), load(indices[n+2])-load(indices[n]));
                    Need(XMVectorGetX(XMVector3LengthSq(normal)) > 1e-16f, "Degenerate triangle cannot generate flat normal");
                    XMFLOAT3 f; XMStoreFloat3(&f, XMVector3Normalize(normal));
                    for (size_t c = 0; c < 3; ++c) {
                        const auto idx = indices[n+c];
                        flatPos.insert(flatPos.end(), positions.begin()+idx*3, positions.begin()+idx*3+3);
                        flatNormal.insert(flatNormal.end(), {f.x,f.y,f.z});
                        if (!uvs.empty()) flatUv.insert(flatUv.end(), uvs.begin()+idx*2, uvs.begin()+idx*2+2);
                        indices[n+c] = static_cast<uint32_t>(n+c);
                    }
                }
                positions = std::move(flatPos); normals = std::move(flatNormal); uvs = std::move(flatUv);
            }
            const auto vertexBase = static_cast<uint32_t>(imp.asset.vertices.size());
            Need(positions.size()/3 <= 2000000 - imp.asset.vertices.size(), "Flat normal expansion exceeds vertex limit");
            for (size_t n = 0; n < positions.size()/3; ++n) {
                XMFLOAT3 position, normal;
                XMStoreFloat3(&position, XMVector3TransformCoord(XMVectorSet(positions[n*3],positions[n*3+1],positions[n*3+2],1), world));
                auto transformed = XMVector3TransformNormal(XMVectorSet(normals[n*3],normals[n*3+1],normals[n*3+2],0), normalMatrix);
                Need(XMVectorGetX(XMVector3LengthSq(transformed)) > 1e-16f, "Zero transformed normal");
                XMStoreFloat3(&normal, XMVector3Normalize(transformed));
                imp.asset.vertices.push_back({{position.x,position.y,-position.z},{normal.x,normal.y,-normal.z},{uvs.empty()?0:uvs[n*2],uvs.empty()?0:uvs[n*2+1]}});
            }
            const auto first = static_cast<uint32_t>(imp.asset.indices.size());
            for (size_t n = 0; n < indices.size(); n += 3) {
                // Z reflection flips winding; a mirrored node flips it a second time.
                if (determinant > 0) std::swap(indices[n+1], indices[n+2]);
                for (size_t c = 0; c < 3; ++c) imp.asset.indices.push_back(vertexBase + indices[n+c]);
            }
            imp.asset.parts.push_back({first,static_cast<uint32_t>(indices.size()),material});
        }
        for (size_t i = 0; i < node->children_count; ++i) visit(node->children[i], world, depth+1);
    };
    const auto* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene) { for (size_t i = 0; i < scene->nodes_count; ++i) visit(scene->nodes[i], XMMatrixIdentity(), 0); }
    else for (size_t i = 0; i < data->nodes_count; ++i) if (!data->nodes[i].parent) visit(&data->nodes[i], XMMatrixIdentity(), 0);
    ValidateAsset(imp.asset); return std::move(imp.asset);
}
}
