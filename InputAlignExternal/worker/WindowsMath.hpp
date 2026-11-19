// Copyright (c) 2026 dadjdjxjfjdjd-sketch. SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdexcept>
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
namespace gd::platform_math {
// Python 3.12 imports math from the OS UCRT. A statically linked CRT can differ by one ULP.
// Keep C++ allocation static, and resolve only the numerical functions from the same system DLL.
inline HMODULE library() {
    static HMODULE module = LoadLibraryExW(L"ucrtbase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) throw std::runtime_error("System UCRT math unavailable");
    return module; // Process lifetime; one loader reference, never one per simulation step.
}
template<class Function> Function resolve(char const* name) {
    auto function = reinterpret_cast<Function>(GetProcAddress(library(), name));
    if (!function) throw std::runtime_error("System UCRT math export unavailable");
    return function;
}
inline double sin(double x) {
    static auto function = resolve<double(*)(double)>("sin"); return function(x);
}
inline double cos(double x) {
    static auto function = resolve<double(*)(double)>("cos"); return function(x);
}
inline double pow(double x, double y) {
    static auto function = resolve<double(*)(double,double)>("pow"); return function(x,y);
}
}
#endif
