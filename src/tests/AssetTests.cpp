#include "cooker/CookStatic.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace chromalis;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())); Require(bool(out),"Test write failed");
}
void Text(const std::filesystem::path& path, const std::string& text) { Write(path,{text.begin(),text.end()}); }
void Reject(const std::function<void()>& action, const char* what) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(std::string("Expected rejection: ")+what);
}
void U32(std::vector<uint8_t>& bytes, uint32_t v) { for (int n=0;n<4;++n) bytes.push_back(static_cast<uint8_t>(v>>(n*8))); }
void MakePng(const std::filesystem::path& path) {
    using Microsoft::WRL::ComPtr;
    Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"Test COM init");
    {
        ComPtr<IWICImagingFactory> f; Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f))),"Test WIC init");
        ComPtr<IWICStream> stream; Require(SUCCEEDED(f->CreateStream(&stream)),"Test stream");
        Require(SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"Test PNG path");
        ComPtr<IWICBitmapEncoder> encoder; Require(SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)),"Test PNG encoder");
        Require(SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"Test encoder initialize");
        ComPtr<IWICBitmapFrameEncode> frame; Require(SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr)),"Test PNG frame");
        Require(SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(2,2)),"Test PNG size");
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        Require(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA,"Test BGRA support");
        uint8_t rgba[]{0,0,255,255, 0,255,0,255, 255,0,0,255, 0,255,255,255};
        Require(SUCCEEDED(frame->WritePixels(2,8,sizeof(rgba),rgba)),"Test PNG pixels");
        Require(SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()),"Test PNG commit");
    }
    CoUninitialize();
}
std::string SceneJson(const std::string& buffer, const std::string& image, bool flat = false) {
    return std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],
"nodes":[{"mesh":0,"translation":[0,0,1]},{"mesh":1}],
"buffers":[)" )+buffer+R"(],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":128,"byteStride":32},{"buffer":0,"byteOffset":128,"byteLength":12},)"+image+R"(],
"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0]},
{"bufferView":0,"byteOffset":12,"componentType":5126,"count":4,"type":"VEC3"},
{"bufferView":0,"byteOffset":24,"componentType":5126,"count":4,"type":"VEC2"},
{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],
"images":[{"bufferView":2,"mimeType":"image/png"}],"textures":[{"source":0,"sampler":0}],
"samplers":[{"magFilter":9728,"minFilter":9728,"wrapS":33071,"wrapT":33071}],
"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicFactor":0}},
{"pbrMetallicRoughness":{"baseColorFactor":[0,0,1,1]}}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,)"+(flat?"":"\"NORMAL\":1,")+R"("TEXCOORD_0":2},"indices":3,"material":0}]},
{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":3,"material":1}]}]})";
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 3,"Usage: AssetTests external.gltf test-directory");
        const std::filesystem::path root(argv[2]); std::filesystem::create_directories(root);
        MakePng(root/L"corners.png");
        const MeshVertex verts[]{
            {{-1,-1,0},{0,0,1},{0,1}},{{1,-1,0},{0,0,1},{1,1}},
            {{1,1,0},{0,0,1},{1,0}},{{-1,1,0},{0,0,1},{0,0}}};
        const uint16_t indices[]{0,1,2,0,2,3};
        std::vector<uint8_t> binary(sizeof(verts)+sizeof(indices));
        std::memcpy(binary.data(),verts,sizeof(verts)); std::memcpy(binary.data()+sizeof(verts),indices,sizeof(indices));
        const auto png = ReadAssetBytes(root/L"corners.png"); binary.insert(binary.end(),png.begin(),png.end());
        const auto buffer = "{\"byteLength\":"+std::to_string(binary.size())+",\"uri\":\"scene.bin\"}";
        const auto image = "{\"buffer\":0,\"byteOffset\":140,\"byteLength\":"+std::to_string(png.size())+"}";
        auto json = SceneJson(buffer,image); Write(root/L"scene.bin",binary); Text(root/L"scene.gltf",json);
        auto asset = CookStatic(root/L"scene.gltf","tests/corners-depth");
        Require(asset.vertices.size()==8 && asset.parts.size()==2 && asset.materials.size()==3,"Node/primitive/material import failed");
        Require(asset.vertices[0].position[2]==-1 && asset.vertices[0].normal[2]==-1,"RH to LH / node transform failed");
        Require(asset.indices[1]==2 && asset.indices[2]==1,"Winding conversion failed");
        SaveStaticAsset(asset,root/L"scene.cmesh");
        const auto original = ReadAssetBytes(root/L"scene.cmesh");
        const auto loaded = LoadStaticAsset(root/L"scene.cmesh");
        Require(loaded.textures[1].rgba==asset.textures[1].rgba && loaded.id==asset.id,"Cooked roundtrip failed");
        Text(root/L"flat.gltf",SceneJson(buffer,image,true));
        auto flat = CookStatic(root/L"flat.gltf","tests/flat"); Require(flat.vertices.size()==10,"Missing normals not expanded to flat corners");
        // Same asset as GLB, including embedded PNG image and interleaved vertices.
        auto glbJson = SceneJson("{\"byteLength\":"+std::to_string(binary.size())+"}",image);
        while (glbJson.size()%4) glbJson += ' ';
        auto glbBin = binary; while(glbBin.size()%4) glbBin.push_back(0);
        std::vector<uint8_t> glb; U32(glb,0x46546c67); U32(glb,2); U32(glb,static_cast<uint32_t>(28+glbJson.size()+glbBin.size()));
        U32(glb,static_cast<uint32_t>(glbJson.size())); U32(glb,0x4e4f534a); glb.insert(glb.end(),glbJson.begin(),glbJson.end());
        U32(glb,static_cast<uint32_t>(glbBin.size())); U32(glb,0x004e4942); glb.insert(glb.end(),glbBin.begin(),glbBin.end()); Write(root/L"scene.glb",glb);
        const auto fromGlb = CookStatic(root/L"scene.glb","tests/glb");
        Require(fromGlb.indices==asset.indices && fromGlb.textures[1].rgba==asset.textures[1].rgba,"GLB import differs from glTF");
        SaveStaticAsset(fromGlb,root/L"glb.cmesh");
        const auto nodeText = std::string("[0,0,1]"); auto updated = json; updated.replace(updated.find(nodeText),nodeText.size(),"[0,0,2]");
        Text(root/L"scene.gltf",updated); auto changed = CookStatic(root/L"scene.gltf",asset.id);
        Require(changed.vertices[0].position[2]==-2 && changed.dependencies[0].sha256 != asset.dependencies[0].sha256,"Source update not reflected");
        SaveStaticAsset(changed,root/L"changed.cmesh");
        Text(root/L"scene.gltf","{ invalid");
        Reject([&] { SaveStaticAsset(CookStatic(root/L"scene.gltf",asset.id),root/L"scene.cmesh"); },"Malformed source");
        Require(ReadAssetBytes(root/L"scene.cmesh")==original,"Failed cook changed last-good output");
        Text(root/L"scene.gltf",json);
        auto corrupted = original; corrupted[8] = 99; Write(root/L"invalid.cmesh",corrupted);
        Reject([&] { (void)LoadStaticAsset(root/L"invalid.cmesh"); },"Unknown version");
        corrupted.resize(20); Write(root/L"invalid.cmesh",corrupted);
        Reject([&] { (void)LoadStaticAsset(root/L"invalid.cmesh"); },"Truncated cooked package");
        auto invalid = asset; invalid.indices[0] = 99999;
        Reject([&] { SaveStaticAsset(invalid,root/L"scene.cmesh"); },"Out-of-range index");
        Require(ReadAssetBytes(root/L"scene.cmesh")==original,"Rejected save changed existing package");
        auto missing = json; missing.replace(missing.find("scene.bin"),9,"missing.bin"); Text(root/L"missing.gltf",missing);
        Reject([&] { (void)CookStatic(root/L"missing.gltf",asset.id); },"Missing external buffer");
        auto outside = json; outside.replace(outside.find("scene.bin"),9,"../outside.bin"); Text(root/L"outside.gltf",outside);
        Reject([&] { (void)CookStatic(root/L"outside.gltf",asset.id); },"Path escape");
        auto external = CookStatic(argv[1],"samples/cube"); SaveStaticAsset(external,root/L"cube.cmesh");
        Require(external.textures.size()>1 && external.vertices.size()>3,"External textured Cube import failed");
        std::cout << "PASS: external glTF, embedded GLB, UV/image roundtrip, node/winding conversion, flat normals, source change, failed-cook retention, corrupt/version/index/path rejection\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
