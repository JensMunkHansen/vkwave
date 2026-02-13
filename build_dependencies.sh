#!/bin/bash

# build_dependencies.sh
# Build script for dependencies using NativeDeps/CMakeLists.txt
# Builds both Debug and Release by default

set -e

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m'

# Default values
COMPILER="gcc"
BUILD_TYPE=""
VERBOSE=""
FORCE=""
VKWAVEPKG_ROOT="${VKWAVEPKG_ROOT:-$HOME/vkwavepkg}"

# Functions for colored output
print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_success() { echo -e "${GREEN}✅ $1${NC}"; }
print_error()   { echo -e "${RED}❌ $1${NC}"; }
print_warning() { echo -e "${YELLOW}⚠️  $1${NC}"; }
print_info()    { echo -e "${CYAN}ℹ️  $1${NC}"; }
print_step()    { echo -e "${PURPLE}🔧 $1${NC}"; }

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --compiler)
                COMPILER="$2"
                shift 2
                ;;
            --type)
                BUILD_TYPE="$2"
                shift 2
                ;;
            --verbose)
                VERBOSE="--verbose"
                shift
                ;;
            --force)
                FORCE="--force"
                shift
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done
}

# Show help message
show_help() {
    echo -e "${WHITE}build_dependencies.sh - Build dependencies for vkwave${NC}"
    echo ""
    echo -e "${WHITE}Usage: $0 [options]${NC}"
    echo ""
    echo -e "${WHITE}Options:${NC}"
    echo -e "  ${CYAN}--compiler COMPILER${NC}    Compiler to use (clang|gcc) [default: gcc]"
    echo -e "  ${CYAN}--type BUILD_TYPE${NC}      Build type (Debug|Release) [default: both]"
    echo -e "  ${CYAN}--verbose${NC}              Enable verbose output"
    echo -e "  ${CYAN}--force${NC}                Force rebuild of dependencies"
    echo -e "  ${CYAN}--help${NC}                 Show this help message"
    echo ""
    echo -e "${WHITE}Environment Variables:${NC}"
    echo -e "  ${CYAN}VKWAVEPKG_ROOT${NC}        Root directory for dependencies [default: \$HOME/vkwavepkg]"
    echo ""
    echo -e "${WHITE}Examples:${NC}"
    echo -e "  $0                                    # Use defaults (gcc, Debug+Release)"
    echo -e "  $0 --compiler clang                   # Build with clang"
    echo -e "  $0 --type Release                     # Build Release only"
    echo -e "  $0 --force --verbose                  # Force rebuild with verbose output"
}

# Validate inputs
validate_inputs() {
    print_step "Validating inputs..."

    if [[ "$COMPILER" != "clang" && "$COMPILER" != "gcc" ]]; then
        print_error "Invalid compiler: $COMPILER. Must be 'clang' or 'gcc'"
        exit 1
    fi

    if [[ -n "$BUILD_TYPE" && "$BUILD_TYPE" != "Debug" && "$BUILD_TYPE" != "Release" ]]; then
        print_error "Invalid build type: $BUILD_TYPE. Must be 'Debug' or 'Release'"
        exit 1
    fi

    if [[ ! -d "NativeDeps" ]]; then
        print_error "NativeDeps directory not found"
        exit 1
    fi

    if [[ ! -f "NativeDeps/CMakeLists.txt" ]]; then
        print_error "NativeDeps/CMakeLists.txt not found"
        exit 1
    fi

    print_success "Input validation completed"
}

# Setup directories
setup_directories() {
    print_step "Setting up directories..."

    INSTALL_DIR="$VKWAVEPKG_ROOT/linux/$COMPILER/install"
    BUILD_DIR="$VKWAVEPKG_ROOT/linux/$COMPILER/build"

    print_info "Install directory: $INSTALL_DIR"
    print_info "Build directory: $BUILD_DIR"

    mkdir -p "$INSTALL_DIR"
    mkdir -p "$BUILD_DIR"

    print_success "Directories created"
}

# Check if dependencies already exist
check_existing_dependencies() {
    if [[ -z "$FORCE" ]]; then
        print_step "Checking for existing dependencies..."

        local all_found=true

        for dep_name in \
            "Catch2:lib/cmake/Catch2/Catch2Config.cmake" \
            "spdlog:lib/cmake/spdlog/spdlogConfig.cmake" \
            "GLFW:lib/cmake/glfw3/glfw3Config.cmake" \
            "imgui:lib/cmake/imgui/imguiConfig.cmake" \
            "cgltf:include/cgltf.h" \
            "stb:include/stb/stb_image.h"; do

            local name="${dep_name%%:*}"
            local path="${dep_name##*:}"

            if [[ -f "$INSTALL_DIR/$path" ]]; then
                print_success "$name already installed"
            else
                all_found=false
            fi
        done

        if [[ "$all_found" == true ]]; then
            print_success "All dependencies already exist. Use --force to rebuild."
            exit 0
        else
            print_warning "Some dependencies missing. Proceeding with build..."
        fi
    else
        print_warning "Force rebuild requested. Proceeding with build..."
    fi
}

# Build dependencies for a specific build type
build_for_type() {
    local build_type="$1"
    print_step "Building dependencies ($build_type)..."

    local cmake_args=(
        "-DCMAKE_BUILD_TYPE=$build_type"
        "-DCMAKE_INSTALL_PREFIX=$INSTALL_DIR"
        "-DBUILD_SHARED_LIBS=ON"
        "-DBUILD_CATCH2=ON"
        "-DBUILD_SPDLOG=ON"
        "-DBUILD_GLFW=ON"
        "-DBUILD_IMGUI=ON"
        "-DBUILD_CGLTF=ON"
        "-DBUILD_STB=ON"
    )

    if [[ "$COMPILER" == "gcc" ]]; then
        cmake_args+=("-DCMAKE_CXX_COMPILER=g++" "-DCMAKE_C_COMPILER=gcc")
    else
        cmake_args+=("-DCMAKE_CXX_COMPILER=clang++" "-DCMAKE_C_COMPILER=clang")
    fi

    if [[ -n "$VERBOSE" ]]; then
        cmake_args+=("-DDEPS_VERBOSE=ON")
    fi

    local type_build_dir="$BUILD_DIR/$build_type"

    print_step "Configuring ($build_type)..."
    cmake -S NativeDeps -B "$type_build_dir" "${cmake_args[@]}"

    print_step "Building ($build_type)..."
    if [[ -n "$VERBOSE" ]]; then
        cmake --build "$type_build_dir" --parallel --verbose
    else
        cmake --build "$type_build_dir" --parallel
    fi

    print_success "$build_type build completed"
}

# Build dependencies (both Release and Debug, or a specific type)
build_dependencies() {
    if [[ -n "$BUILD_TYPE" ]]; then
        build_for_type "$BUILD_TYPE"
    else
        build_for_type Release
        build_for_type Debug
    fi
}

# Verify installation
verify_installation() {
    print_step "Verifying installation..."

    local all_good=true

    for dep_name in \
        "Catch2:lib/cmake/Catch2/Catch2Config.cmake" \
        "spdlog:lib/cmake/spdlog/spdlogConfig.cmake" \
        "GLFW:lib/cmake/glfw3/glfw3Config.cmake" \
        "imgui:lib/cmake/imgui/imguiConfig.cmake" \
        "cgltf:include/cgltf.h" \
        "stb:include/stb/stb_image.h"; do

        local name="${dep_name%%:*}"
        local path="${dep_name##*:}"

        if [[ -f "$INSTALL_DIR/$path" ]]; then
            print_success "$name installation verified"
        else
            print_error "$name installation failed (missing $path)"
            all_good=false
        fi
    done

    if [[ "$all_good" == true ]]; then
        print_success "All dependencies verified successfully"
    else
        print_error "Some dependencies failed to install"
        exit 1
    fi
}

# Clean build directory
clean_build_directory() {
    print_step "Cleaning build directory..."
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
        print_success "Build directory removed, keeping only install directory"
    fi
}

# Show installation summary
show_summary() {
    print_header "Build Summary"
    echo -e "${WHITE}Configuration:${NC}"
    echo -e "  Compiler: ${CYAN}$COMPILER${NC}"
    if [[ -n "$BUILD_TYPE" ]]; then
        echo -e "  Build Type: ${CYAN}$BUILD_TYPE${NC}"
    else
        echo -e "  Build Types: ${CYAN}Release + Debug${NC}"
    fi
    echo -e "  Install Directory: ${CYAN}$INSTALL_DIR${NC}"
    echo ""
    echo -e "${WHITE}Installed Dependencies:${NC}"
    echo -e "  Catch2, spdlog, GLFW, Dear ImGui, cgltf, stb"
    echo ""
    echo -e "${WHITE}Files installed in:${NC}"
    echo -e "  ${CYAN}$INSTALL_DIR/lib${NC}       - Libraries"
    echo -e "  ${CYAN}$INSTALL_DIR/include${NC}   - Header files"
    echo -e "  ${CYAN}$INSTALL_DIR/lib/cmake${NC} - CMake config files"
    echo ""
    print_success "Build completed successfully!"
}

# Main execution
main() {
    print_header "vkwave Dependencies Builder"

    parse_arguments "$@"

    print_info "Compiler: $COMPILER"
    if [[ -n "$BUILD_TYPE" ]]; then
        print_info "Build type: $BUILD_TYPE"
    else
        print_info "Build types: Release + Debug"
    fi
    print_info "VKWAVEPKG_ROOT: $VKWAVEPKG_ROOT"

    if [[ -n "$VERBOSE" ]]; then
        print_info "Verbose mode: enabled"
    fi
    if [[ -n "$FORCE" ]]; then
        print_info "Force rebuild: enabled"
    fi

    echo ""

    validate_inputs
    setup_directories
    check_existing_dependencies
    build_dependencies
    verify_installation
    clean_build_directory
    show_summary
}

main "$@"
