/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_test_macros.hpp>

#include "SKL/ABIX/Interface/Interface.h"

namespace {

using skl::abix::poly::abix_manifest_version;
using skl::abix::poly::module_state;
using skl::abix::poly::ModuleControl;
using skl::abix::poly::ModuleManifest;
using skl::abix::poly::ModuleRegistry;
using skl::abix::poly::ModuleState;
using skl::abix::poly::ModuleStatus;

int destroyed = 0;
void destroy_object(void *object) {
    ++destroyed;
    delete static_cast<int *>(object);
}

ModuleManifest empty_manifest() { return {abix_manifest_version, 1, 0, 0, nullptr, 0, 0, nullptr}; }

}   // namespace

TEST_CASE("owned handles release through the creator's destroy entry") {
    destroyed = 0;
    {
        skl::abix::owner::ref owner =
            skl::abix::owner::ref::adopt(skl::abix::owner::make_owned(new int(7), &destroy_object));
        REQUIRE(owner.valid());
        REQUIRE(owner.owns());
        REQUIRE(owner.ownership() == skl::abix::owner::mode::owned);
        REQUIRE(*static_cast<int *>(owner.object()) == 7);
        REQUIRE_FALSE(owner.leased());
    }
    REQUIRE(destroyed == 1);

    // A default handle is invalid; releasing it is a no-op.
    skl::abix::owner::ref empty;
    REQUIRE_FALSE(empty.valid());
    empty.release();
    empty.release();
    REQUIRE(destroyed == 1);
}

TEST_CASE("releasing twice destroys once") {
    destroyed = 0;
    skl::abix::owner::ref owner =
        skl::abix::owner::ref::adopt(skl::abix::owner::make_owned(new int(1), &destroy_object));
    owner.release();
    owner.release();
    REQUIRE(destroyed == 1);
    REQUIRE_FALSE(owner.valid());
    REQUIRE(owner.object() == nullptr);
}

TEST_CASE("borrowed handles are never destroyed by the borrower") {
    destroyed = 0;
    int local = 5;
    {
        skl::abix::owner::ref borrowed = skl::abix::owner::ref::borrow(&local);
        REQUIRE(borrowed.valid());
        REQUIRE_FALSE(borrowed.owns());
        REQUIRE(borrowed.ownership() == skl::abix::owner::mode::borrowed);
    }
    REQUIRE(destroyed == 0);
}

TEST_CASE("copied handles own their independent copy") {
    destroyed = 0;
    {
        skl::abix::owner::ref copy =
            skl::abix::owner::ref::adopt(skl::abix::owner::make_copied(new int(9), &destroy_object));
        REQUIRE(copy.owns());
        REQUIRE(copy.ownership() == skl::abix::owner::mode::copied);
    }
    REQUIRE(destroyed == 1);
}

TEST_CASE("ownership transfers by move, destroying exactly once") {
    destroyed = 0;
    skl::abix::owner::ref source =
        skl::abix::owner::ref::adopt(skl::abix::owner::make_owned(new int(3), &destroy_object));
    skl::abix::owner::ref destination = std::move(source);
    REQUIRE_FALSE(source.valid());
    REQUIRE(destination.valid());
    destination.release();
    REQUIRE(destroyed == 1);
    REQUIRE_FALSE(destination.valid());
}

TEST_CASE("an owned object keeps its module loaded until released") {
    ModuleRegistry<> registry;
    ModuleControl control;
    control.name = "owner-module";
    REQUIRE(registry.register_module(control, empty_manifest()) == ModuleStatus::ok);

    skl::abix::owner::ref owner =
        skl::abix::owner::ref::adopt(skl::abix::owner::make_owned(new int(11), &destroy_object, control.id), &control);
    REQUIRE(owner.valid());
    REQUIRE(owner.leased());
    // While the object is alive, its creating module cannot unload.
    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::busy);
    REQUIRE(module_state(control) == ModuleState::draining);

    destroyed = 0;
    owner.release();
    REQUIRE(destroyed == 1);
    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::ok);
    REQUIRE(module_state(control) == ModuleState::unloaded);
}
