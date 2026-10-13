# Phase 0B static asset contract

Scene導入後の所有権・複数モデル配置・描画リストは[scene.md](scene.md)を正とする。以下の2スロット記述は0B時点のプレビューで、現在は旧CLI互換設定だけに残る。静的ファイル形式は変更していない。

## Responsibilities

`ChromalisAssetCooker` uses **cgltf 1.15 (MIT)** to parse/validate glTF 2.0 and GLB, and **Windows Imaging Component** from the selected Windows SDK to decode PNG/JPEG. It links to the CPU-only `ChromalisAssets` library. Runtime links only to `ChromalisAssets`; it neither parses glTF nor requires cgltf/WIC/Editor/ImGui. Cooker can be excluded with `-DCHROMALIS_BUILD_COOKER=OFF` (no cgltf fetch).

cgltf is small, C-based and covers the required accessors, buffers and node structures without introducing a general asset framework. CMake pins v1.15 and archive SHA256 `84e352092e5cd6aab7f66de62ddb66beb5e6f18d412ca9d12950d7a55bfef25a`; its MIT license is copied to `licenses/cgltf.txt` beside Cooker. An identical offline archive is accepted through `CHROMALIS_CGLTF_ARCHIVE`. WIC and DirectXMath already belong to the pinned Windows SDK 10.0.26100.0; no additional texture/math package is needed.

## Supported import subset

- glTF/GLB default scene (first scene if no default), static node hierarchy, multiple triangle primitives and materials. Node transforms are baked into vertices.
- Indexed or non-indexed triangle lists; u8/u16/u32 indices. Interleaved, normalized and sparse floating-point attributes are unpacked by cgltf. Sparse indices are rejected.
- POSITION, NORMAL, TEXCOORD_0. Missing normals produce split flat-shaded corners; missing UV is allowed only without a base-color texture.
- External relative buffers/images, embedded GLB images and base64 data URIs. Local dependencies must remain under the source directory. No network URI or path escape.
- PNG/JPEG decoded to straight RGBA8. Base-color texture/factor, repeat/clamp/mirror, nearest/linear (chosen from magFilter), opaque/mask, double-sided, KHR_materials_unlit. Metallic/roughness scalar values are retained but not evaluated by the preview shader.

Skinning, animations, morph targets, Draco primitives, non-triangle primitives, alpha BLEND, texture transforms, nonzero base-color UV sets and unsupported required extensions (including meshopt) fail with a diagnostic. Optional meshopt decoding is not performed; only standard fallback buffer data can be used. Optional extra PBR maps/extensions and vertex colors are not evaluated; the success report explicitly states the base-color-only preview limitation. This is not a full glTF PBR renderer. Texture minification currently follows the same nearest/linear choice as magnification; there are no mips, GPU compression, streaming or color-profile conversion. Source base-color images follow glTF's sRGB contract.

## Coordinates, color and drawing

Runtime is left handed, +Y up, +Z forward, meters. Cooker reflects glTF Z, transforms normals by inverse transpose, and adjusts triangle winding including mirrored nodes. UV/image rows are not flipped. CPU DirectXMath uses row vectors; DXC `-Zpr` and HLSL `row_major` use `position * world * view * projection`. The LH camera looks at the origin; projection uses depth 0..1 and D32 LESS depth testing. Preview world scale is positive/uniform; imported nodes may have nonsingular nonuniform/mirrored scale.

Node invertibility is checked using the determinant of unit-length basis vectors (double precision, dimensionless threshold 1e-7), rather than absolute world determinant magnitude. Small uniform scale and composed parent/child scales are accepted. Zero, nonfinite and nearly collinear bases are rejected. The inverse-transpose normal matrix is rescaled by a common positive factor before float conversion and normal normalization. This avoids rejecting ordinary millimeter-scale assets while retaining a numerical conditioning guard. Values that cannot be represented in the float runtime contract remain unsupported.

`alphaCutoff` is finite and nonnegative with no upper bound of 1. It is retained exactly as a float, including values greater than 1: MASK then discards every fragment with glTF alpha in [0,1]. Metallic/roughness still require [0,1].

Base-color texture SRVs are sRGB and factors/lighting are linear. Phase 0C moves output encoding from the mesh shader to an R8G8B8A8_UNORM_SRGB RTV on the existing SDR UNORM swap chain, allowing linear VFX blending. Opaque output and cutout masks only. The 0A diagnostic triangle and ImGui remain display-color paths. Preview lighting is a single simple directional term with an unlit toggle, without shadows or PBR. See [animation-vfx.md](animation-vfx.md) for the current rendering contract.

Two independently transformed preview slots exercise multiple model drawing. They are a 0B host/UI limit, not a future world/entity design. A model contains all its primitive/material bindings.

## Cooked file and regeneration

`.cmesh` is one bounded binary package: `CHRMESH\0` magic, uint32 version **1**, stable ASCII asset ID, dependency metadata, vertices, uint32 indices, primitive ranges, materials and RGBA8 images. Integers and IEEE754 floats are explicitly serialized little endian. Count prefixes precede arrays; strings are UTF-8 bytes with uint32 lengths. The material record is 56 bytes. No ABI struct dumps or GPU pointers are saved. Unknown versions, truncated/trailing data, nonfinite values, invalid counts/ranges and invalid references are rejected before GPU allocation.

The recipe and every external source dependency have SHA256 metadata. Embedded data is covered by its containing source hash. Explicit recook always rebuilds; there is no implicit timestamp cache or watcher. Change a source/dependency or Cooker recipe → recook with the same asset ID → reload. Changes to format require a new version and recook, not speculative migration APIs. Runtime does not reread source hashes or need source files.

ID expresses content identity; slot index locates a preview instance; generation counts successful runtime replacements. These are separate from GPU addresses and descriptors. IDs do not currently resolve through a global database.

Cooker writes a temporary sibling file, reads/validates it, then publishes with Windows atomic replacement. Failure retains the prior package. CLI refuses to overwrite a source dependency. Temporary files may remain after a process crash and can be removed manually. Imports are bounded to 512 MiB per file/combined buffers, 2M vertices, 6M indices, 1024 materials, 256 textures, 8192 dimensions/16M pixels per image, 256 MiB decoded image total. These are adjustable data-validation limits, not performance targets or promises of handling hostile inputs with fixed memory use.

CLI `--report path` is optional. Only after a successful import establishes the complete dependency set does Cooker check output/report against input and dependencies, and output against report (canonical path, Windows case aliases, existing hard links). No report is opened or truncated before this validation. Import/argument/collision failures write diagnostics to stdout and return 1, leaving any previous report untouched. A validated report is opened without truncation, then updated only after cmesh publication; its parent directory must exist. Report I/O failure returns 1 and explicitly states if cmesh has already been published. Consumers must check the exit code rather than treating an old report as the current result. The Editor captures this invocation's stdout/stderr through a pipe and does not create or initialize a report file.

The import recipe is now `recipe2` to identify the revised transform/material validation. The binary layout and format version remain 1. Recook sources to use the revised import behavior; existing valid version-1 packages still load.

## GPU ownership and replacement

`Renderer` owns each model's default-heap VB/IB/textures and shader-visible SRV/sampler heaps. The CPU package is a temporary value released after upload. Material/primitive metadata survives in the GPU model. No caller owns raw GPU-resource references.

Loading builds a complete candidate without altering the live slot. A local direct-queue command list uploads buffers/images and records transitions. After submission, the graphics queue is drained via the existing fence. Only then are the live model and candidate swapped: previous draws, new uploads, allocators, upload buffers and old descriptors are all complete before release. Failures before submission preserve the previous live model. A device/fence failure is fatal, not a recoverable asset update; automatic device recovery remains outside this phase.

Reload is synchronous and can pause the Editor. This is a deliberate 0B tradeoff. Per-frame allocator/upload reuse still requires that frame's fence. ImGui keeps its separate existing heap/retirement rules, rebound after model rendering. Depth is shared on the single ordered graphics queue and recreated after queue drain on resize. Runtime resource-state transitions remain inside Renderer, not Cooker or UI.

## Phase 0C boundary

0Cの実装は別形式`.cskin`とEffekseerを追加した。以下は0B時点の境界判断を残したもの。現在の仕様は[animation-vfx.md](animation-vfx.md)を参照。

Do not derive a skeleton or animation format from flattened static vertices. 0C must validate a representative rig and decide how node hierarchy, skeleton, animation clips and socket transforms are represented before changing the cooked version. Retain the offline-import/runtime-load separation and fence lifetime contract. VFX textures need their own linear/sRGB and alpha semantics; base-color sRGB is not a blanket rule for noise/masks. Mips/compression/asynchronous loading should follow measured content needs. No animation or VFX runtime is integrated in 0B.

References: [cgltf v1.15](https://github.com/jkuhlmann/cgltf/tree/v1.15), [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html), [D3D12 resource barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).
