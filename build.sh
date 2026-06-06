#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUBMODULE_DIR="${ROOT_DIR}/third_party/mediamtx-rpicamera-fork"
BUILD_DIR="${ROOT_DIR}/build"
MEDIAMTX_BUILD_DIR="${BUILD_DIR}/mediamtx-rpicamera-fork"
MEDIAMTX_INSTALL_DIR="${MEDIAMTX_BUILD_DIR}/install"
DST_DIR="${ROOT_DIR}/dst"
OUTPUT_BIN="${DST_DIR}/RpiCamTs"
BACKEND_BIN="${MEDIAMTX_INSTALL_DIR}/bin/mtxrpicam"
DEPLOY_ARCHIVE="${TMPDIR:-/tmp}/RpiCamTs-dst.tar.gz"

log() {
    echo "[RpiCamTs] $*"
}

ensure_submodule() {
    log "Initializing/updating git submodule..."

    git submodule update --init --recursive

    if [[ ! -d "${SUBMODULE_DIR}" ]]; then
        echo "Error: submodule directory not found: ${SUBMODULE_DIR}" >&2
        exit 1
    fi
}

require_command() {
    local command_name="$1"

    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Error: required command not found: ${command_name}" >&2
        exit 1
    fi
}

build_mediamtx_rpicamera_fork() {
    local git_commit_template="${SUBMODULE_DIR}/git_commit.h.in"
    local build_status=0
    local remove_git_commit_template=0
    local setup_status=0

    log "Building mediamtx-rpicamera-fork..."

    require_command meson
    require_command ninja
    require_command git

    # The redirect wrap in the prototype requires libpisp to exist before
    # Meson parses the complete subproject set.
    if [[ ! -f "${SUBMODULE_DIR}/subprojects/libpisp/subprojects/nlohmann_json.wrap" ]]; then
        log "Downloading the libpisp subproject..."

        if [[ -e "${SUBMODULE_DIR}/subprojects/libpisp" ]]; then
            echo "Error: incomplete libpisp subproject already exists" >&2
            echo "Remove ${SUBMODULE_DIR}/subprojects/libpisp and run the build again." >&2
            exit 1
        fi

        git clone \
            --branch v1.2.0 \
            --depth 1 \
            https://github.com/raspberrypi/libpisp.git \
            "${SUBMODULE_DIR}/subprojects/libpisp"
        git -C "${SUBMODULE_DIR}/subprojects/libpisp" apply \
            "${SUBMODULE_DIR}/subprojects/packagefiles/libpisp.patch"
    fi

    # The selected prototype branch references this template but does not
    # include it. Keep the compatibility file only for the Meson build.
    if [[ ! -f "${git_commit_template}" ]]; then
        printf '%s\n' '#define GIT_COMMIT "@GIT_COMMIT@"' >"${git_commit_template}"
        remove_git_commit_template=1
    fi

    if [[ -f "${MEDIAMTX_BUILD_DIR}/build.ninja" ]]; then
        meson setup --reconfigure "${MEDIAMTX_BUILD_DIR}" "${SUBMODULE_DIR}" || setup_status=$?
    elif [[ -d "${MEDIAMTX_BUILD_DIR}" ]]; then
        meson setup --wipe "${MEDIAMTX_BUILD_DIR}" "${SUBMODULE_DIR}" || setup_status=$?
    else
        meson setup "${MEDIAMTX_BUILD_DIR}" "${SUBMODULE_DIR}" || setup_status=$?
    fi

    if ((setup_status != 0)); then
        if ((remove_git_commit_template)); then
            rm -f "${git_commit_template}"
        fi
        return "${setup_status}"
    fi

    rm -rf "${MEDIAMTX_INSTALL_DIR}"
    DESTDIR="${MEDIAMTX_INSTALL_DIR}" \
        ninja -C "${MEDIAMTX_BUILD_DIR}" install || build_status=$?

    if ((remove_git_commit_template)); then
        rm -f "${git_commit_template}"
    fi

    if ((build_status != 0)); then
        return "${build_status}"
    fi
}

generate_build_files() {
    log "Generating CMake build files..."

    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "Unix Makefiles"
}

build_project() {
    log "Building RpiCamTs..."

    cmake --build "${BUILD_DIR}"
}

install_binary() {
    log "Installing binaries to dst/..."

    mkdir -p "${DST_DIR}"

    if [[ ! -x "${BUILD_DIR}/RpiCamTs" ]]; then
        echo "Error: built binary not found: ${BUILD_DIR}/RpiCamTs" >&2
        exit 1
    fi

    rm -f "${DST_DIR}/RpiCamTs"
    cp "${BUILD_DIR}/RpiCamTs" "${OUTPUT_BIN}"

    log "Done: ${OUTPUT_BIN}"

    if [[ ! -x "${BACKEND_BIN}" ]]; then
        echo "Error: built backend not found: ${BACKEND_BIN}" >&2
        exit 1
    fi

    rm -f "${DST_DIR}/mtxrpicam"
    cp "${BACKEND_BIN}" "${DST_DIR}/mtxrpicam"

    log "Done: ${DST_DIR}/mtxrpicam"
}

install_mediamtx_shared_libraries() {
    local library
    local library_count=0

    log "Installing mediamtx-rpicamera-fork shared libraries to dst/..."
    mkdir -p "${DST_DIR}"
    find "${DST_DIR}" -maxdepth 1 \
        \( -type f -o -type l \) \
        \( -name '*.so' -o -name '*.so.[0-9]*' -o -name '*.so.sign' \) \
        -delete

    while IFS= read -r -d '' library; do
        cp -a "${library}" "${DST_DIR}/"
        library_count=$((library_count + 1))
    done < <(
        find "${MEDIAMTX_INSTALL_DIR}" \
            \( -type f -o -type l \) \
            \( -name '*.so' -o -name '*.so.[0-9]*' -o -name '*.so.sign' \) \
            ! -path '*/libcamera/ipa_*.so*' \
            -print0
    )

    log "Installed ${library_count} shared library file(s) to ${DST_DIR}"
}

install_runtime_data() {
    local ipa_module_source="${MEDIAMTX_INSTALL_DIR}/lib/aarch64-linux-gnu/libcamera"
    local ipa_source="${MEDIAMTX_INSTALL_DIR}/share/libcamera/ipa"
    local pisp_config_source="${MEDIAMTX_INSTALL_DIR}/share/libpisp/backend_default_config.json"

    log "Installing camera runtime data to dst/share/..."

    if [[ ! -d "${ipa_module_source}" ]]; then
        echo "Error: libcamera IPA modules not found: ${ipa_module_source}" >&2
        exit 1
    fi
    if [[ ! -d "${ipa_source}" ]]; then
        echo "Error: libcamera IPA configuration not found: ${ipa_source}" >&2
        exit 1
    fi
    if [[ ! -f "${pisp_config_source}" ]]; then
        echo "Error: libpisp backend configuration not found: ${pisp_config_source}" >&2
        exit 1
    fi

    rm -rf "${DST_DIR}/libcamera" "${DST_DIR}/share"
    mkdir -p "${DST_DIR}/libcamera" "${DST_DIR}/share/libcamera" \
        "${DST_DIR}/share/libpisp"
    cp -a "${ipa_module_source}"/ipa_rpi_*.so* "${DST_DIR}/libcamera/"
    cp -a "${ipa_source}" "${DST_DIR}/share/libcamera/"
    cp "${pisp_config_source}" "${DST_DIR}/share/libpisp/"
}

create_deployment_archive() {
    log "Creating deployment archive..."

    require_command tar
    rm -f "${DEPLOY_ARCHIVE}"
    tar -C "${ROOT_DIR}" -czf "${DEPLOY_ARCHIVE}" "$(basename "${DST_DIR}")"

    log "Done: ${DEPLOY_ARCHIVE}"
}

main() {
    ensure_submodule
    build_mediamtx_rpicamera_fork
    generate_build_files
    build_project
    install_binary
    install_mediamtx_shared_libraries
    install_runtime_data
    create_deployment_archive
}

main "$@"
