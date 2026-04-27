#!/usr/bin/env python3
"""Generate a test glTF: oblate spheroid with thickness + transmission textures.

Squashed sphere (oblate spheroid):
  - Poles (top/bottom) are thin — light passes through
  - Equator is thick — opaque

Textures:
  - Thickness: geometric chord length through ellipsoid (varies with latitude)
  - Transmission: gradient from 0 to 1 (varies with longitude)

Output: data/SSSTestSphere/SSSTestSphere.gltf + .bin + PNGs
"""

import json
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# ── Parameters ──────────────────────────────────────────────────────────────
RINGS = 64              # latitude subdivisions
SEGMENTS = 128          # longitude subdivisions
RADIUS = 1.0            # equatorial radius
SQUASH = 0.3            # polar radius = SQUASH * RADIUS (0.3 = very flat)
TEX_SIZE = 512

# Material
BASE_COLOR = [0.9, 0.7, 0.6, 1.0]   # warm skin-like tone
METALLIC = 0.0
ROUGHNESS = 0.4
ATTENUATION_COLOR = [0.8, 0.2, 0.15]
ATTENUATION_DISTANCE = 0.5

SCRIPT_DIR = Path(__file__).parent
OUT_DIR = SCRIPT_DIR.parent / "data" / "SSSTestSphere"
IMG_DIR = OUT_DIR / "SSSTestSphere_images"


# ── Mesh Generation ────────────────────────────────────────────────────────
def make_ellipsoid(rings, segments, radius, squash):
    """Create an oblate spheroid with positions, normals, tangents, UVs, and indices."""
    positions = []
    normals = []
    tangents = []
    uvs = []

    a = radius          # equatorial semi-axis (x, z)
    b = radius * squash  # polar semi-axis (y)

    for i in range(rings + 1):
        phi = math.pi * i / rings       # 0 (top) to pi (bottom)
        v = i / rings
        sp = math.sin(phi)
        cp = math.cos(phi)

        for j in range(segments + 1):
            theta = 2.0 * math.pi * j / segments
            u = j / segments
            st = math.sin(theta)
            ct = math.cos(theta)

            # Surface point
            x = a * sp * ct
            y = b * cp
            z = a * sp * st

            # Outward normal: gradient of x²/a² + y²/b² + z²/a² = 1
            nx_u = sp * ct / a
            ny_u = cp / b
            nz_u = sp * st / a
            g = math.sqrt(nx_u * nx_u + ny_u * ny_u + nz_u * nz_u)
            if g > 1e-8:
                nx_u /= g
                ny_u /= g
                nz_u /= g

            # Tangent: dP/dtheta (normalized), w=1.0 handedness
            tx = -sp * st
            tz = sp * ct
            ty = 0.0
            tlen = math.sqrt(tx * tx + ty * ty + tz * tz)
            if tlen > 1e-8:
                tx /= tlen
                ty /= tlen
                tz /= tlen
            else:
                tx, ty, tz = 1.0, 0.0, 0.0  # pole fallback

            positions.append([x, y, z])
            normals.append([nx_u, ny_u, nz_u])
            tangents.append([tx, ty, tz, 1.0])
            uvs.append([u, v])

    # Triangle indices
    indices = []
    for i in range(rings):
        for j in range(segments):
            v0 = i * (segments + 1) + j
            v1 = v0 + 1
            v2 = (i + 1) * (segments + 1) + j
            v3 = v2 + 1
            indices.extend([v0, v2, v1])
            indices.extend([v1, v2, v3])

    return (np.array(positions, dtype=np.float32),
            np.array(normals, dtype=np.float32),
            np.array(tangents, dtype=np.float32),
            np.array(uvs, dtype=np.float32),
            np.array(indices, dtype=np.uint32))


# ── Texture Generation ──────────────────────────────────────────────────────
def make_thickness_texture(tex_size, radius, squash):
    """Analytical thickness: chord length through ellipsoid along inward normal.

    Derivation (ray-ellipsoid intersection for surface point with own normal):
      thickness(phi) = 2 * g^3 / (sin²(phi)/a⁴ + cos²(phi)/b⁴)
    where g = sqrt(sin²(phi)/a² + cos²(phi)/b²), a = equatorial, b = polar.

    Poles → thin (2b), equator → thick (2a).
    """
    a = radius
    b = radius * squash
    a2 = a * a
    b2 = b * b
    a4 = a2 * a2
    b4 = b2 * b2

    v = np.linspace(0, 1, tex_size)  # v=0 top pole, v=1 bottom pole
    phi = v * np.pi

    sp2 = np.sin(phi) ** 2
    cp2 = np.cos(phi) ** 2

    g2 = sp2 / a2 + cp2 / b2
    g = np.sqrt(g2)
    g3 = g ** 3

    denom = sp2 / a4 + cp2 / b4
    # Avoid division by zero at exact poles (denom → 1/b⁴ > 0, so safe)
    thickness = 2.0 * g3 / denom  # physical thickness in world units

    # Normalize to [0, 1]
    t_min = thickness.min()
    t_max = thickness.max()
    print(f"  Thickness range: {t_min:.4f} .. {t_max:.4f} (world units)")
    print(f"  Pole chord = {2*b:.3f}, Equator chord = {2*a:.3f}")
    thickness_norm = (thickness - t_min) / (t_max - t_min) if t_max > t_min else thickness

    # Broadcast to 2D (same for all u columns — only varies with v/latitude)
    thickness_2d = np.tile(thickness_norm[:, np.newaxis], (1, tex_size))

    # Pack into PNG: R=255 (AO=1.0), G=thickness, B=0
    img = np.zeros((tex_size, tex_size, 3), dtype=np.uint8)
    img[:, :, 0] = 255                                       # R = AO
    img[:, :, 1] = (thickness_2d * 255).astype(np.uint8)     # G = thickness
    return img


def make_transmission_texture(tex_size):
    """Left-to-right gradient for per-pixel transmission.

    Stored as RGBA: RGB=white, A=transmission factor.
    Left (u=0) → A=0 (opaque), right (u=1) → A=1 (fully transmissive).
    """
    u = np.linspace(0, 1, tex_size)
    gradient = np.tile(u[np.newaxis, :], (tex_size, 1))

    img = np.ones((tex_size, tex_size, 4), dtype=np.uint8) * 255
    img[:, :, 3] = (gradient * 255).astype(np.uint8)  # A channel = transmission
    return img


# ── glTF Builder ────────────────────────────────────────────────────────────
def build_gltf(positions, normals, tangents, uvs, indices, out_dir, img_dir_name):
    """Write .gltf JSON + .bin + reference PNGs."""
    num_vertices = len(positions)
    num_indices = len(indices)

    # Compute bounds
    pos_min = positions.min(axis=0).tolist()
    pos_max = positions.max(axis=0).tolist()

    # Binary data sections (4-byte aligned)
    def align4(data):
        r = len(data) % 4
        return data + b"\x00" * (4 - r) if r else data

    pos_bytes = align4(positions.tobytes())
    norm_bytes = align4(normals.tobytes())
    tan_bytes = align4(tangents.tobytes())
    uv_bytes = align4(uvs.tobytes())
    idx_bytes = align4(indices.tobytes())

    pos_off = 0
    norm_off = pos_off + len(pos_bytes)
    tan_off = norm_off + len(norm_bytes)
    uv_off = tan_off + len(tan_bytes)
    idx_off = uv_off + len(uv_bytes)
    total = idx_off + len(idx_bytes)

    bin_data = pos_bytes + norm_bytes + tan_bytes + uv_bytes + idx_bytes

    gltf = {
        "asset": {"version": "2.0", "generator": "generate_sss_test_sphere.py"},
        "extensionsUsed": [
            "KHR_materials_diffuse_transmission",
            "KHR_materials_volume"
        ],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "SSSTestSphere"}],
        "meshes": [{
            "name": "SSSTestSphere",
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
            {"bufferView": 0, "componentType": 5126, "count": num_vertices,
             "type": "VEC3", "min": pos_min, "max": pos_max},
            {"bufferView": 1, "componentType": 5126, "count": num_vertices,
             "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": num_vertices,
             "type": "VEC4"},
            {"bufferView": 3, "componentType": 5126, "count": num_vertices,
             "type": "VEC2"},
            {"bufferView": 4, "componentType": 5125, "count": num_indices,
             "type": "SCALAR"}
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_off,  "byteLength": num_vertices * 12, "target": 34962},
            {"buffer": 0, "byteOffset": norm_off,  "byteLength": num_vertices * 12, "target": 34962},
            {"buffer": 0, "byteOffset": tan_off,   "byteLength": num_vertices * 16, "target": 34962},
            {"buffer": 0, "byteOffset": uv_off,    "byteLength": num_vertices * 8,  "target": 34962},
            {"buffer": 0, "byteOffset": idx_off,   "byteLength": num_indices * 4,   "target": 34963},
        ],
        "buffers": [{"uri": "SSSTestSphere.bin", "byteLength": total}],
        "materials": [{
            "name": "sss_test",
            "pbrMetallicRoughness": {
                "baseColorFactor": BASE_COLOR,
                "metallicFactor": METALLIC,
                "roughnessFactor": ROUGHNESS
            },
            "doubleSided": True,
            "extensions": {
                "KHR_materials_diffuse_transmission": {
                    "diffuseTransmissionFactor": 1.0,
                    "diffuseTransmissionTexture": {"index": 1}
                },
                "KHR_materials_volume": {
                    "thicknessFactor": 1.0,
                    "thicknessTexture": {"index": 0},
                    "attenuationColor": ATTENUATION_COLOR,
                    "attenuationDistance": ATTENUATION_DISTANCE
                }
            }
        }],
        "textures": [
            {"source": 0, "sampler": 0},  # thickness
            {"source": 1, "sampler": 0},  # transmission
        ],
        "images": [
            {"uri": f"{img_dir_name}/thickness.png", "mimeType": "image/png"},
            {"uri": f"{img_dir_name}/transmission.png", "mimeType": "image/png"},
        ],
        "samplers": [{
            "magFilter": 9729,   # LINEAR
            "minFilter": 9987,   # LINEAR_MIPMAP_LINEAR
            "wrapS": 33071,      # CLAMP_TO_EDGE
            "wrapT": 33071
        }]
    }

    bin_path = out_dir / "SSSTestSphere.bin"
    gltf_path = out_dir / "SSSTestSphere.gltf"

    with open(bin_path, "wb") as f:
        f.write(bin_data)
    print(f"  Wrote {bin_path} ({len(bin_data)} bytes)")

    with open(gltf_path, "w") as f:
        json.dump(gltf, f, indent=2)
    print(f"  Wrote {gltf_path}")


# ── Main ────────────────────────────────────────────────────────────────────
def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    IMG_DIR.mkdir(parents=True, exist_ok=True)

    # 1. Generate mesh
    print(f"Generating ellipsoid: {RINGS} rings x {SEGMENTS} segments, "
          f"R={RADIUS}, squash={SQUASH}")
    positions, normals, tangents, uvs, indices = make_ellipsoid(
        RINGS, SEGMENTS, RADIUS, SQUASH)
    print(f"  Vertices: {len(positions)}, Triangles: {len(indices)//3}")

    # 2. Thickness texture (analytical)
    print("Generating thickness texture...")
    thickness_img = make_thickness_texture(TEX_SIZE, RADIUS, SQUASH)
    Image.fromarray(thickness_img).save(IMG_DIR / "thickness.png")
    print(f"  Saved {IMG_DIR / 'thickness.png'}")

    # 3. Transmission texture (gradient)
    print("Generating transmission texture...")
    transmission_img = make_transmission_texture(TEX_SIZE)
    Image.fromarray(transmission_img).save(IMG_DIR / "transmission.png")
    print(f"  Saved {IMG_DIR / 'transmission.png'}")

    # 4. Build glTF
    print("Building glTF...")
    build_gltf(positions, normals, tangents, uvs, indices, OUT_DIR,
               "SSSTestSphere_images")

    print(f"\nDone! Output in: {OUT_DIR}")
    print("Add to TOML models list:")
    print(f'  "@DATA_DIR@/SSSTestSphere/SSSTestSphere.gltf"')


if __name__ == "__main__":
    main()
