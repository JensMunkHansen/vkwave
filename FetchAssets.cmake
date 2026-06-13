# FetchAssets.cmake — Download glTF assets and HDR environments at configure time
#
# Assets are fetched from:
#   Models: https://github.com/KhronosGroup/glTF-Sample-Assets (main branch)
#   HDR:    https://github.com/KhronosGroup/glTF-Sample-Environments (low_resolution_hdrs branch)
#
# To add a new GLB model, append its name to _GLB_MODELS below.
# To add a new HDR environment, append its name to _HDR_ENVIRONMENTS below.

option(VKWAVE_FETCH_ASSETS "Download glTF sample assets at configure time" ON)

if(NOT VKWAVE_FETCH_ASSETS)
  message(STATUS "Asset fetching disabled (VKWAVE_FETCH_ASSETS=OFF)")
  return()
endif()

set(_GLTF_ASSETS_BASE "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models")
set(_HDR_ENV_BASE "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Environments/low_resolution_hdrs")
set(_DATA_DIR "${CMAKE_SOURCE_DIR}/data")

# ---------------------------------------------------------------------------
# Helper: download a single file if it doesn't already exist
# ---------------------------------------------------------------------------
function(_download_asset url dest)
  if(EXISTS "${dest}")
    return()
  endif()
  get_filename_component(_dir "${dest}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dir}")
  message(STATUS "Downloading ${url}")
  file(DOWNLOAD "${url}" "${dest}"
    STATUS _status
    SHOW_PROGRESS
  )
  list(GET _status 0 _code)
  if(NOT _code EQUAL 0)
    list(GET _status 1 _msg)
    message(WARNING "Failed to download ${url}: ${_msg}")
    file(REMOVE "${dest}")
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Download a GLB model: Models/<name>/glTF-Binary/<name>.glb
# ---------------------------------------------------------------------------
function(fetch_gltf_model_glb name)
  _download_asset(
    "${_GLTF_ASSETS_BASE}/${name}/glTF-Binary/${name}.glb"
    "${_DATA_DIR}/${name}/glTF-Binary/${name}.glb"
  )
endfunction()

# ---------------------------------------------------------------------------
# Download a multi-file (non-binary) glTF model: Models/<name>/glTF/<file>...
# Some assets (e.g. FlightHelmet) ship only as separate .gltf + .bin + textures,
# with no glTF-Binary/.glb variant. Pass every file in the glTF/ folder.
# ---------------------------------------------------------------------------
function(fetch_gltf_model_multifile name)
  foreach(_file IN LISTS ARGN)
    _download_asset(
      "${_GLTF_ASSETS_BASE}/${name}/glTF/${_file}"
      "${_DATA_DIR}/${name}/glTF/${_file}"
    )
  endforeach()
endfunction()

# ---------------------------------------------------------------------------
# Download an HDR environment map
# ---------------------------------------------------------------------------
function(fetch_hdr_environment name)
  _download_asset(
    "${_HDR_ENV_BASE}/${name}.hdr"
    "${_DATA_DIR}/${name}.hdr"
  )
endfunction()

# ===========================================================================
# Asset lists — add new entries here
# ===========================================================================

# GLB models
set(_GLB_MODELS
  DamagedHelmet
  CompareMetallic
  AlphaBlendModeTest
  ScatteringSkull
  ClearCoatTest      # KHR_materials_clearcoat conformance test (base/coat/coated columns)
  ClearCoatCarPaint  # car-paint clearcoat demo
  CompareAnisotropy  # KHR_materials_anisotropy A/B (with vs without)
  AnisotropyDiscTest # KHR_materials_anisotropy texture-driven direction
  CarConcept         # multi-extension concept car (clearcoat, anisotropy, etc.)
  ABeautifulGame     # chess set on a flat board — texture mip / minification demo
  TextureCoordinateTest # flat UV-grid plane — canonical minification-aliasing demo
  TransmissionTest   # KHR_materials_transmission conformance test (glass grid)
)

foreach(_model IN LISTS _GLB_MODELS)
  fetch_gltf_model_glb(${_model})
endforeach()

# Multi-file glTF models (no glTF-Binary/.glb variant in the repo).
# FlightHelmet ships as a .gltf manifest + .bin geometry + per-material PNGs.
fetch_gltf_model_multifile(FlightHelmet
  FlightHelmet.gltf
  FlightHelmet.bin
  FlightHelmet_Materials_GlassPlasticMat_BaseColor.png
  FlightHelmet_Materials_GlassPlasticMat_Normal.png
  FlightHelmet_Materials_GlassPlasticMat_OcclusionRoughMetal.png
  FlightHelmet_Materials_LeatherPartsMat_BaseColor.png
  FlightHelmet_Materials_LeatherPartsMat_Normal.png
  FlightHelmet_Materials_LeatherPartsMat_OcclusionRoughMetal.png
  FlightHelmet_Materials_LensesMat_BaseColor.png
  FlightHelmet_Materials_LensesMat_Normal.png
  FlightHelmet_Materials_LensesMat_OcclusionRoughMetal.png
  FlightHelmet_Materials_MetalPartsMat_BaseColor.png
  FlightHelmet_Materials_MetalPartsMat_Normal.png
  FlightHelmet_Materials_MetalPartsMat_OcclusionRoughMetal.png
  FlightHelmet_Materials_RubberWoodMat_BaseColor.png
  FlightHelmet_Materials_RubberWoodMat_Normal.png
  FlightHelmet_Materials_RubberWoodMat_OcclusionRoughMetal.png
)

# HDR environments (Khronos glTF-Sample-Environments)
set(_HDR_ENVIRONMENTS
  neutral
  footprint_court
)

foreach(_env IN LISTS _HDR_ENVIRONMENTS)
  fetch_hdr_environment(${_env})
endforeach()

# ---------------------------------------------------------------------------
# Download an HDR environment from Poly Haven (CC0 licensed)
# Uses 1k resolution — sufficient for IBL cubemap generation
# ---------------------------------------------------------------------------
set(_POLYHAVEN_HDR_BASE "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k")

function(fetch_polyhaven_hdr name)
  _download_asset(
    "${_POLYHAVEN_HDR_BASE}/${name}_1k.hdr"
    "${_DATA_DIR}/${name}.hdr"
  )
endfunction()

# Poly Haven HDR environments (CC0 licensed)
fetch_polyhaven_hdr(hospital_room)
fetch_polyhaven_hdr(hospital_room_2)
fetch_polyhaven_hdr(surgery)
fetch_polyhaven_hdr(childrens_hospital)
