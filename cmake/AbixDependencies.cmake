# AbixDependencies.cmake
#
# Generalized dependency management for the ABIX project.
#
# All third-party dependencies are discovered through CMake's package
# configuration system (find_package CONFIG mode).  This module provides
# a single entry point for locating all external packages that the ABIX
# project (or its consumers) may need, with per-component REQUIRED/OPTIONAL
# control.
#
# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
#
#   # Simplest — include to find all optional dependencies (backward-compatible):
#   include(cmake/AbixDependencies.cmake)
#
#   # Or call with specific components:
#   abix_find_dependencies(COMPONENTS LLVM Catch2 benchmark)
#
#   # Mark certain components as REQUIRED (others remain optional):
#   abix_find_dependencies(REQUIRED LLVM Catch2)
#
# ---------------------------------------------------------------------------
# Supported component names
# ---------------------------------------------------------------------------
#   LLVM       — LLVM + Clang + the clang-cpp library target
#   Catch2     — Catch2 v3 (or later) testing framework
#   benchmark  — Google Benchmark
#   fmt        — fmtlib output formatting library
#
# ---------------------------------------------------------------------------
# Variables set after inclusion / function call
# ---------------------------------------------------------------------------
#   ABIX_LLVM_FOUND       TRUE if LLVM + Clang + the clang-cpp library target
#                           are all available.
#   ABIX_Catch2_FOUND     TRUE if Catch2 v3 (or later) was found.
#   ABIX_benchmark_FOUND  TRUE if Google Benchmark was found.
#   ABIX_FMT_FOUND        TRUE if fmtlib was found.
#
#   LLVM_DIR              Path to LLVMConfig.cmake directory detected by
#                           find_package(CONFIG); set via -DLLVM_DIR=... to
#                           override automatic search.
#   Clang_DIR             Path to ClangConfig.cmake directory (set alongside
#                           LLVM_DIR; configure manually if needed).
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

# ============================================================================
# Public entry point: abix_find_dependencies()
# ============================================================================

macro(abix_find_dependencies)
  # Because we need to set variables in the caller's scope (not just one
  # level up), we use a macro (which runs in the caller's scope directly)
  # instead of a function.
  #
  # Usage:  abix_find_dependencies([COMPONENTS <dep1> <dep2> ...]
  #                                 [REQUIRED  <dep1> <dep2> ...])
  #
  # If COMPONENTS is omitted, all known dependencies are searched.

  cmake_parse_arguments(_abix "" "" "COMPONENTS;REQUIRED" ${ARGN})

  # If no COMPONENTS specified, default to all known dependencies.
  set(_abix_all_deps LLVM Catch2 benchmark fmt)
  if(NOT _abix_COMPONENTS)
    set(_abix_COMPONENTS ${_abix_all_deps})
  endif()

  # --------------------------------------------------------------------------
  # LLVM + Clang
  # --------------------------------------------------------------------------
  if(LLVM IN_LIST _abix_COMPONENTS)
    list(FIND _abix_REQUIRED LLVM _abix_llvm_req)
    if(_abix_llvm_req GREATER -1)
      set(_abix_llvm_quiet "")
    else()
      set(_abix_llvm_quiet "QUIET")
    endif()

    # Always clear CACHE entries so every configure run performs a
    # fresh search.  This ensures LLVM/Clang are re-detected when the
    # environment changes (e.g. different toolchain, platform switch).
    unset(LLVM_DIR CACHE)
    unset(Clang_DIR CACHE)

    find_package(LLVM CONFIG ${_abix_llvm_quiet})
    find_package(Clang CONFIG ${_abix_llvm_quiet})

    if(LLVM_FOUND AND TARGET clang-cpp)
      set(ABIX_LLVM_FOUND TRUE)
      message(STATUS "LLVM ${LLVM_PACKAGE_VERSION} — ${LLVM_DIR}")
      message(STATUS "Clang          — ${Clang_DIR}")
    else()
      set(ABIX_LLVM_FOUND FALSE)
      if(LLVM_FOUND AND NOT TARGET clang-cpp)
        message(STATUS "LLVM found but clang-cpp library target is missing "
                        "(install the full LLVM package including libclang-cpp)")
      elseif(_abix_llvm_req GREATER -1)
        message(FATAL_ERROR "LLVM / Clang not found — required by the project configuration.  "
                            "Install LLVM via Homebrew, or set LLVM_DIR / Clang_DIR on the command line.")
      else()
        message(STATUS "LLVM / Clang not found — AMC (ABI Metadata Compiler) "
                        "will not be built.  Install LLVM via Homebrew, or set "
                        "LLVM_DIR / Clang_DIR on the command line.")
      endif()
    endif()
  endif()

  # --------------------------------------------------------------------------
  # fmtlib
  # --------------------------------------------------------------------------
  if(fmt IN_LIST _abix_COMPONENTS)
    list(FIND _abix_REQUIRED fmt _abix_fmt_req)
    if(_abix_fmt_req GREATER -1)
      set(_abix_fmt_quiet "")
    else()
      set(_abix_fmt_quiet "QUIET")
    endif()

    find_package(fmt CONFIG ${_abix_fmt_quiet})

    if(fmt_FOUND)
      set(ABIX_FMT_FOUND TRUE)
      message(STATUS "fmt ${FMT_VERSION} found — ${FMT_DIR}")
    else()
      set(ABIX_FMT_FOUND FALSE)
      if(_abix_fmt_req GREATER -1)
        message(FATAL_ERROR "fmt not found — required by the project configuration.  "
                            "Install: brew install fmt")
      else()
        message(STATUS "fmt not found — install via: brew install fmt")
      endif()
    endif()
  endif()

  # --------------------------------------------------------------------------
  # Catch2  (testing framework)
  # --------------------------------------------------------------------------
  if(Catch2 IN_LIST _abix_COMPONENTS)
    list(FIND _abix_REQUIRED Catch2 _abix_catch2_req)
    if(_abix_catch2_req GREATER -1)
      set(_abix_catch2_quiet "")
    else()
      set(_abix_catch2_quiet "QUIET")
    endif()

    find_package(Catch2 3 CONFIG ${_abix_catch2_quiet})

    if(Catch2_FOUND)
      set(ABIX_Catch2_FOUND TRUE)
      message(STATUS "Catch2 ${Catch2_VERSION} found")
    else()
      set(ABIX_Catch2_FOUND FALSE)
      if(_abix_catch2_req GREATER -1)
        message(FATAL_ERROR "Catch2 v3 not found — required by the project configuration.  "
                            "Install: brew install catch2")
      else()
        message(STATUS "Catch2 v3 not found — tests will not be built.  "
                        "Install: brew install catch2")
      endif()
    endif()
  endif()

  # --------------------------------------------------------------------------
  # Google Benchmark
  # --------------------------------------------------------------------------
  if(benchmark IN_LIST _abix_COMPONENTS)
    list(FIND _abix_REQUIRED benchmark _abix_bench_req)
    if(_abix_bench_req GREATER -1)
      set(_abix_bench_quiet "")
    else()
      set(_abix_bench_quiet "QUIET")
    endif()

    find_package(benchmark CONFIG ${_abix_bench_quiet})

    if(benchmark_FOUND)
      set(ABIX_benchmark_FOUND TRUE)
      message(STATUS "Google Benchmark found")
    else()
      set(ABIX_benchmark_FOUND FALSE)
      if(_abix_bench_req GREATER -1)
        message(FATAL_ERROR "Google Benchmark not found — required by the project configuration.  "
                            "Install: brew install google-benchmark")
      else()
        message(STATUS "Google Benchmark not found — benchmarks will not be built.  "
                        "Install: brew install google-benchmark")
      endif()
    endif()
  endif()

  # Clean up internal variables so they don't leak into the caller's scope.
  unset(_abix_all_deps)
  unset(_abix_COMPONENTS)
  unset(_abix_REQUIRED)
  unset(_abix_llvm_req)
  unset(_abix_llvm_quiet)
  unset(_abix_fmt_req)
  unset(_abix_fmt_quiet)
  unset(_abix_catch2_req)
  unset(_abix_catch2_quiet)
  unset(_abix_bench_req)
  unset(_abix_bench_quiet)
endmacro()

# ============================================================================
# Auto-run on include (backward-compatible default)
# ============================================================================
abix_find_dependencies()