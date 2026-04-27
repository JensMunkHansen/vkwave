#!/usr/bin/env python3
"""Convert a PLY dental scan with vertex colors to glTF with SSS textures.

Produces:
  LowerJawScan/LowerJawScan.gltf   — glTF JSON
  LowerJawScan/LowerJawScan.bin    — geometry binary
  LowerJawScan/LowerJawScan_images/baseColor.png   — 2048x2048 sRGB
  LowerJawScan/LowerJawScan_images/thickness.png   — 2048x2048 linear (G channel)

Thickness map semantics:
  - Teeth: thick in center (opaque), thin at edges (translucent SSS)
  - Gingiva: thick everywhere (no light passes through the jaw)
  - Thickness=0 only for unmapped/background pixels
"""

import json
import struct
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

# ── Tunable constants ────────────────────────────────────────────────────────
TEX_SIZE = 2048

# HSV-based teeth segmentation:
#   Teeth are bright (high V) and unsaturated (low S) — white/ivory.
#   Gingiva is pink/red — higher saturation, lower value.
TEETH_SAT_MAX = 0.45          # teeth have low saturation (white/ivory/yellow)
TEETH_VAL_MIN = 0.35          # teeth are brighter than gingiva
MORPH_KERNEL_SIZE = 5
MORPH_ITERATIONS = 2

THICKNESS_POWER = 0.7         # sub-linear falloff curve
GINGIVA_THICKNESS = 0.85      # gingiva = thick (no light through jaw)
ROUGHNESS = 0.4
ATTENUATION_COLOR = [0.8, 0.2, 0.15]   # warm reddish (blood/tissue)
ATTENUATION_DISTANCE = 0.5

SCRIPT_DIR = Path(__file__).parent
PLY_PATH = SCRIPT_DIR / "VertexColorLowerJawScan.ply"
OUT_DIR = SCRIPT_DIR / "LowerJawScan"
IMG_DIR = OUT_DIR / "LowerJawScan_images"


# ── Step 1: Load PLY ────────────────────────────────────────────────────────
def load_ply(path):
    """Parse binary little-endian PLY with vertices (pos+color) and faces (indices+UVs)."""
    print(f"Loading PLY: {path}")
    with open(path, "rb") as f:
        # Parse header
        header_lines = []
        while True:
            line = f.readline().decode("ascii").strip()
            header_lines.append(line)
            if line == "end_header":
                break

        num_vertices = 0
        num_faces = 0
        for line in header_lines:
            if line.startswith("element vertex"):
                num_vertices = int(line.split()[-1])
            elif line.startswith("element face"):
                num_faces = int(line.split()[-1])

        print(f"  Vertices: {num_vertices}, Faces: {num_faces}")

        # Read vertices: 3 floats (xyz) + 3 uchars (rgb)
        vertex_stride = 3 * 4 + 3 * 1  # 15 bytes per vertex
        vertex_data = f.read(num_vertices * vertex_stride)
        dt = np.dtype([
            ("x", "<f4"), ("y", "<f4"), ("z", "<f4"),
            ("r", "u1"), ("g", "u1"), ("b", "u1"),
        ])
        vertices = np.frombuffer(vertex_data, dtype=dt)

        positions = np.column_stack([vertices["x"], vertices["y"], vertices["z"]]).astype(np.float32)
        colors = np.column_stack([vertices["r"], vertices["g"], vertices["b"]]).astype(np.float32) / 255.0

        # Read faces: each face has vertex_indices list + texcoord list
        face_indices = []
        face_uvs = []  # per-face-vertex UVs
        for _ in range(num_faces):
            # vertex_indices: uchar count + count * int32
            n_verts = struct.unpack("<B", f.read(1))[0]
            indices = struct.unpack(f"<{n_verts}i", f.read(n_verts * 4))
            face_indices.append(indices)

            # texcoords: uchar count + count * float
            n_tc = struct.unpack("<B", f.read(1))[0]
            tc_flat = struct.unpack(f"<{n_tc}f", f.read(n_tc * 4))
            # tc_flat is [u0, v0, u1, v1, u2, v2, ...]
            uvs = [(tc_flat[i * 2], tc_flat[i * 2 + 1]) for i in range(n_tc // 2)]
            face_uvs.append(uvs)

    face_indices = np.array(face_indices, dtype=np.int32)  # (num_faces, 3)
    print(f"  Face array shape: {face_indices.shape}")

    # Collapse per-face UVs to per-vertex (1:1 mapping, no seam splits)
    uvs = np.zeros((num_vertices, 2), dtype=np.float32)
    for fi in range(num_faces):
        for vi in range(3):
            idx = face_indices[fi, vi]
            uvs[idx] = face_uvs[fi][vi]

    return positions, colors, face_indices, uvs


# ── Step 2: Compute Normals ─────────────────────────────────────────────────
def compute_normals(positions, faces):
    """Area-weighted vertex normals."""
    print("Computing normals...")
    v0 = positions[faces[:, 0]]
    v1 = positions[faces[:, 1]]
    v2 = positions[faces[:, 2]]
    face_normals = np.cross(v1 - v0, v2 - v0)  # area-weighted (not normalized)

    vertex_normals = np.zeros_like(positions)
    np.add.at(vertex_normals, faces[:, 0], face_normals)
    np.add.at(vertex_normals, faces[:, 1], face_normals)
    np.add.at(vertex_normals, faces[:, 2], face_normals)

    lengths = np.linalg.norm(vertex_normals, axis=1, keepdims=True)
    lengths = np.maximum(lengths, 1e-8)
    vertex_normals /= lengths
    return vertex_normals.astype(np.float32)


# ── Step 3: Bake Vertex Colors to Texture ────────────────────────────────────
def bake_texture(positions, colors, faces, uvs, tex_size):
    """Rasterize triangles in UV space to bake vertex colors into a texture."""
    print(f"Baking vertex colors to {tex_size}x{tex_size} texture...")
    t0 = time.time()

    texture = np.zeros((tex_size, tex_size, 3), dtype=np.float32)
    mask = np.zeros((tex_size, tex_size), dtype=bool)

    uv0 = uvs[faces[:, 0]]
    uv1 = uvs[faces[:, 1]]
    uv2 = uvs[faces[:, 2]]
    c0 = colors[faces[:, 0]]
    c1 = colors[faces[:, 1]]
    c2 = colors[faces[:, 2]]

    num_faces = len(faces)
    for fi in range(num_faces):
        if fi % 50000 == 0 and fi > 0:
            print(f"  Triangle {fi}/{num_faces} ({fi*100//num_faces}%)")

        # Triangle vertices in pixel coords
        p0 = uv0[fi] * tex_size
        p1 = uv1[fi] * tex_size
        p2 = uv2[fi] * tex_size

        # Bounding box
        min_x = max(0, int(np.floor(min(p0[0], p1[0], p2[0]))))
        max_x = min(tex_size - 1, int(np.ceil(max(p0[0], p1[0], p2[0]))))
        min_y = max(0, int(np.floor(min(p0[1], p1[1], p2[1]))))
        max_y = min(tex_size - 1, int(np.ceil(max(p0[1], p1[1], p2[1]))))

        if max_x < min_x or max_y < min_y:
            continue

        # Generate pixel grid within bounding box
        xs = np.arange(min_x, max_x + 1) + 0.5
        ys = np.arange(min_y, max_y + 1) + 0.5
        px, py = np.meshgrid(xs, ys)

        # Barycentric coordinates
        d00 = p1[0] - p0[0]
        d01 = p2[0] - p0[0]
        d10 = p1[1] - p0[1]
        d11 = p2[1] - p0[1]
        denom = d00 * d11 - d01 * d10
        if abs(denom) < 1e-10:
            continue

        inv_denom = 1.0 / denom
        dx = px - p0[0]
        dy = py - p0[1]
        u = (dx * d11 - d01 * dy) * inv_denom
        v = (d00 * dy - dx * d10) * inv_denom
        w = 1.0 - u - v

        inside = (u >= -1e-4) & (v >= -1e-4) & (w >= -1e-4)
        if not np.any(inside):
            continue

        iy, ix = np.where(inside)
        pixel_x = ix + min_x
        pixel_y = iy + min_y

        u_vals = u[inside]
        v_vals = v[inside]
        w_vals = w[inside]

        # Interpolate colors
        color = (w_vals[:, None] * c0[fi] +
                 u_vals[:, None] * c1[fi] +
                 v_vals[:, None] * c2[fi])

        texture[pixel_y, pixel_x] = color
        mask[pixel_y, pixel_x] = True

    elapsed = time.time() - t0
    print(f"  Baked {np.sum(mask)} pixels in {elapsed:.1f}s")

    # Dilate to fill UV seam gaps (2px)
    for c in range(3):
        channel = texture[:, :, c]
        for _ in range(2):
            dilated = ndimage.maximum_filter(channel, size=3)
            unfilled = ~mask
            channel[unfilled] = dilated[unfilled]
        texture[:, :, c] = channel
        # Update mask after dilation
    mask = ndimage.binary_dilation(mask, iterations=2)

    return texture, mask


# ── Step 4 & 5: Teeth Segmentation (HSV) + Thickness Map ────────────────────
def make_thickness_map(texture, mask, tex_size):
    """Segment teeth via HSV thresholding, generate thickness via distance transform.

    Teeth: bright (high V) and unsaturated (low S) — white/ivory color.
    Gingiva: pink/red — higher saturation.

    Thickness semantics:
      - Teeth: distance transform from edge → thick center, thin edges (SSS at edges)
      - Gingiva: uniformly thick (no light passes through the entire jaw)
      - Background: 0
    """
    print("Generating thickness map...")

    # RGB → HSV (per-pixel)
    cmax = np.max(texture, axis=2)
    cmin = np.min(texture, axis=2)
    chroma = cmax - cmin

    # Saturation = chroma / max (0 when max=0)
    saturation = np.where(cmax > 1e-6, chroma / cmax, 0.0)
    # Value = max channel
    value = cmax

    # Teeth: low saturation AND high value (bright white/ivory)
    teeth_mask = (saturation < TEETH_SAT_MAX) & (value > TEETH_VAL_MIN) & mask
    # Gingiva: everything else that's mapped
    gingiva_mask = mask & ~teeth_mask

    # Morphological cleanup on teeth mask
    kernel = np.ones((MORPH_KERNEL_SIZE, MORPH_KERNEL_SIZE), dtype=bool)
    teeth_mask = ndimage.binary_erosion(teeth_mask, structure=kernel, iterations=MORPH_ITERATIONS)
    teeth_mask = ndimage.binary_dilation(teeth_mask, structure=kernel, iterations=MORPH_ITERATIONS)
    # Recompute gingiva after cleanup
    gingiva_mask = mask & ~teeth_mask

    # Distance transform on teeth mask → thick center, thin edges
    dist = ndimage.distance_transform_edt(teeth_mask)
    max_dist = dist.max()
    if max_dist > 0:
        dist = dist / max_dist
    dist = np.power(dist, THICKNESS_POWER)

    # Compose thickness: teeth = distance-based, gingiva = uniformly thick
    thickness = np.zeros((tex_size, tex_size), dtype=np.float32)
    thickness[teeth_mask] = dist[teeth_mask]
    thickness[gingiva_mask] = GINGIVA_THICKNESS

    # Pack: R=255 (AO=1.0), G=thickness, B=0
    thickness_img = np.zeros((tex_size, tex_size, 3), dtype=np.uint8)
    thickness_img[:, :, 0] = 255
    thickness_img[:, :, 1] = (thickness * 255).astype(np.uint8)

    teeth_count = np.sum(teeth_mask)
    gingiva_count = np.sum(gingiva_mask)
    print(f"  Teeth pixels: {teeth_count} ({teeth_count * 100 / (tex_size * tex_size):.1f}%)")
    print(f"  Gingiva pixels: {gingiva_count} ({gingiva_count * 100 / (tex_size * tex_size):.1f}%)")
    print(f"  Max distance: {max_dist:.1f} pixels")

    return thickness_img


# ── Step 6: Build glTF ──────────────────────────────────────────────────────
def build_gltf(positions, normals, uvs, faces, out_dir, img_dir_name):
    """Write .gltf JSON + .bin binary + reference PNG textures."""
    print("Building glTF...")

    num_vertices = len(positions)
    num_indices = faces.size  # total index count

    # Default tangents (1,0,0,1) — no normal map
    tangents = np.zeros((num_vertices, 4), dtype=np.float32)
    tangents[:, 0] = 1.0
    tangents[:, 3] = 1.0

    # Flatten indices to uint32
    indices = faces.flatten().astype(np.uint32)

    # Compute accessor bounds
    pos_min = positions.min(axis=0).tolist()
    pos_max = positions.max(axis=0).tolist()

    # Build binary buffer
    pos_bytes = positions.tobytes()
    norm_bytes = normals.tobytes()
    tan_bytes = tangents.tobytes()
    uv_bytes = uvs.tobytes()
    idx_bytes = indices.tobytes()

    # Pad each section to 4-byte alignment
    def align4(data):
        remainder = len(data) % 4
        if remainder:
            data += b"\x00" * (4 - remainder)
        return data

    pos_bytes = align4(pos_bytes)
    norm_bytes = align4(norm_bytes)
    tan_bytes = align4(tan_bytes)
    uv_bytes = align4(uv_bytes)
    idx_bytes = align4(idx_bytes)

    # Buffer view offsets
    pos_offset = 0
    norm_offset = pos_offset + len(pos_bytes)
    tan_offset = norm_offset + len(norm_bytes)
    uv_offset = tan_offset + len(tan_bytes)
    idx_offset = uv_offset + len(uv_bytes)
    total_size = idx_offset + len(idx_bytes)

    bin_data = pos_bytes + norm_bytes + tan_bytes + uv_bytes + idx_bytes

    gltf = {
        "asset": {
            "version": "2.0",
            "generator": "ply_to_gltf.py"
        },
        "extensionsUsed": [
            "KHR_materials_diffuse_transmission",
            "KHR_materials_volume"
        ],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "LowerJawScan"}],
        "meshes": [{
            "name": "LowerJawScan",
            "primitives": [{
                "attributes": {
                    "POSITION": 0,
                    "NORMAL": 1,
                    "TANGENT": 2,
                    "TEXCOORD_0": 3
                },
                "indices": 4,
                "material": 0
            }]
        }],
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,  # FLOAT
                "count": num_vertices,
                "type": "VEC3",
                "min": pos_min,
                "max": pos_max
            },
            {
                "bufferView": 1,
                "componentType": 5126,
                "count": num_vertices,
                "type": "VEC3"
            },
            {
                "bufferView": 2,
                "componentType": 5126,
                "count": num_vertices,
                "type": "VEC4"
            },
            {
                "bufferView": 3,
                "componentType": 5126,
                "count": num_vertices,
                "type": "VEC2"
            },
            {
                "bufferView": 4,
                "componentType": 5125,  # UNSIGNED_INT
                "count": num_indices,
                "type": "SCALAR"
            }
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_offset, "byteLength": num_vertices * 12, "target": 34962},
            {"buffer": 0, "byteOffset": norm_offset, "byteLength": num_vertices * 12, "target": 34962},
            {"buffer": 0, "byteOffset": tan_offset, "byteLength": num_vertices * 16, "target": 34962},
            {"buffer": 0, "byteOffset": uv_offset, "byteLength": num_vertices * 8, "target": 34962},
            {"buffer": 0, "byteOffset": idx_offset, "byteLength": num_indices * 4, "target": 34963},
        ],
        "buffers": [{"uri": "LowerJawScan.bin", "byteLength": total_size}],
        "materials": [{
            "name": "dental_sss",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "metallicFactor": 0.0,
                "roughnessFactor": ROUGHNESS
            },
            "doubleSided": True,
            "extensions": {
                "KHR_materials_diffuse_transmission": {
                    "diffuseTransmissionFactor": 1.0
                },
                "KHR_materials_volume": {
                    "thicknessFactor": 1.0,
                    "thicknessTexture": {"index": 1},
                    "attenuationColor": ATTENUATION_COLOR,
                    "attenuationDistance": ATTENUATION_DISTANCE
                }
            }
        }],
        "textures": [
            {"source": 0, "sampler": 0},  # baseColor
            {"source": 1, "sampler": 0},  # thickness
        ],
        "images": [
            {"uri": f"{img_dir_name}/baseColor.png", "mimeType": "image/png"},
            {"uri": f"{img_dir_name}/thickness.png", "mimeType": "image/png"},
        ],
        "samplers": [{
            "magFilter": 9729,  # LINEAR
            "minFilter": 9987,  # LINEAR_MIPMAP_LINEAR
            "wrapS": 10497,     # REPEAT
            "wrapT": 10497
        }]
    }

    # Write files
    bin_path = out_dir / "LowerJawScan.bin"
    gltf_path = out_dir / "LowerJawScan.gltf"

    with open(bin_path, "wb") as f:
        f.write(bin_data)
    print(f"  Wrote {bin_path} ({len(bin_data)} bytes)")

    with open(gltf_path, "w") as f:
        json.dump(gltf, f, indent=2)
    print(f"  Wrote {gltf_path}")


# ── Main ────────────────────────────────────────────────────────────────────
def main():
    if not PLY_PATH.exists():
        print(f"Error: PLY file not found: {PLY_PATH}")
        sys.exit(1)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    IMG_DIR.mkdir(parents=True, exist_ok=True)

    # Step 1: Load PLY
    positions, colors, faces, uvs = load_ply(PLY_PATH)

    # Step 2: Compute normals
    normals = compute_normals(positions, faces)

    # Step 3: Bake vertex colors to texture
    texture, mask = bake_texture(positions, colors, faces, uvs, TEX_SIZE)

    # Save base color texture (sRGB — vertex colors are already in sRGB)
    base_color_img = (np.clip(texture, 0, 1) * 255).astype(np.uint8)
    Image.fromarray(base_color_img).save(IMG_DIR / "baseColor.png")
    print(f"  Saved {IMG_DIR / 'baseColor.png'}")

    # Steps 4-5: Teeth segmentation (HSV) + thickness map
    thickness_img = make_thickness_map(texture, mask, TEX_SIZE)
    Image.fromarray(thickness_img).save(IMG_DIR / "thickness.png")
    print(f"  Saved {IMG_DIR / 'thickness.png'}")

    # Step 6: Build glTF
    build_gltf(positions, normals, uvs, faces, OUT_DIR, "LowerJawScan_images")

    print("\nDone! Output in:", OUT_DIR)
    print("Add to TOML models list:")
    print(f'  "@DATA_DIR@/../localdata/LowerJawScan/LowerJawScan.gltf"')


if __name__ == "__main__":
    main()
