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
)

foreach(_model IN LISTS _GLB_MODELS)
  fetch_gltf_model_glb(${_model})
endforeach()

# HDR environments
set(_HDR_ENVIRONMENTS
  neutral
  footprint_court
)

foreach(_env IN LISTS _HDR_ENVIRONMENTS)
  fetch_hdr_environment(${_env})
endforeach()
