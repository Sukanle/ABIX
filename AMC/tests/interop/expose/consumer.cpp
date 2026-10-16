// C++ consumer that drives the exposed type through the ABIX registry bus.
//
// It loads the producing module, discovers the interface through the fixed
// `abix_module_query_v1` entry, resolves the method bindings once, and calls
// them through the cached pointers. An object handle holds a module lease, so
// the module cannot be retired while the object is alive.
#include "ABIX/DLL/Object.h"
#include "ABIX/Interface/Interface.h"

#include <cstdint>
#include <cstdio>

using namespace skl::abix;
using namespace skl::abix::poly;

using CreateFn = void *(*)(int);
using GetFn = int (*)(const void *);
using AddFn = void (*)(void *, int);
using DestroyFn = void (*)(void *);

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: consumer <module>\n");
        return 2;
    }
    dll::Object library;
    if (!library.load(argv[1])) {
        std::fprintf(stderr, "load failed\n");
        return 1;
    }

    ModuleRegistry<> registry;
    ModuleControl control;
    control.name = "counter";
    if (!discover_module(library, control, registry).ok()) {
        std::fprintf(stderr, "discover failed\n");
        return 1;
    }
    if (registry.interface_count() != 1) {
        std::fprintf(stderr, "expected one interface\n");
        return 1;
    }
    const ModuleInterface *binding = registry.interface_at(0);

    auto create = reinterpret_cast<CreateFn>(library.resolve("abix_Counter_create"));
    auto destroy = reinterpret_cast<DestroyFn>(library.resolve("abix_Counter_destroy"));
    if (create == nullptr || destroy == nullptr) {
        std::fprintf(stderr, "create/destroy missing\n");
        return 1;
    }
    void *object = create(40);
    if (object == nullptr) {
        std::fprintf(stderr, "create returned null\n");
        return 1;
    }

    {
        // Bind once; the lease keeps the module loaded for the call scope.
        auto bound = bind_interface(*binding, object);
        if (!bound.valid()) {
            std::fprintf(stderr, "bind failed\n");
            return 1;
        }
        const auto get = bound.ref.method<GetFn>(0);
        const auto add = bound.ref.method<AddFn>(1);
        if (get == nullptr || add == nullptr) {
            std::fprintf(stderr, "method missing\n");
            return 1;
        }
        if (get(bound.ref.object()) != 40) {
            std::fprintf(stderr, "get before add\n");
            return 1;
        }
        add(bound.ref.object(), 2);
        if (get(bound.ref.object()) != 42) {
            std::fprintf(stderr, "get after add\n");
            return 1;
        }
        // A held lease defers retiring the module.
        if (registry.unregister_module(control.id) != ModuleStatus::busy) {
            std::fprintf(stderr, "module was not held\n");
            return 1;
        }
    }

    // Owner ABI: release through the creator's destroy entry.
    destroy(object);
    if (registry.unregister_module(control.id) != ModuleStatus::ok) {
        std::fprintf(stderr, "unregister failed\n");
        return 1;
    }
    if (!library.unload()) {
        std::fprintf(stderr, "unload failed\n");
        return 1;
    }
    std::printf("expose registry: OK\n");
    return 0;
}
