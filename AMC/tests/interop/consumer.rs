// M1 interop consumer: a real Rust program that links the native demo library
// and calls it through the AMC-generated projection.
//
// The projection (`amc_generated.rs`) is produced by `amc generate -l rust`
// and declares the `#[repr(C)]` type plus the `extern "C"` entry points. Its
// compile-time assertions (size / align / field offsets) are the first line of
// defence: if the library and the metadata disagree, this file fails to build.

#[path = "gen/amc_generated.rs"]
mod abi;

use abi::*;

fn main() {
    unsafe {
        // Pure value call.
        assert_eq!(demo_add(2, 3), 5, "demo_add");

        // Call taking a typed pointer to a value type defined on the Rust side.
        let v = DemoVec2 { x: 4, y: 2.5 };
        let scaled = demo_scale(&v, 2.0);
        assert_eq!(scaled, 13.0, "demo_scale");

        // Ownership wrapper: `create` takes ownership, `drop` releases through
        // the declared destroy entry point -- the consumer never frees foreign
        // memory itself.
        let owned = OwnedDemoVec2::create(7, 1.5).expect("create owned");
        assert_eq!(owned.as_ref().x, 7, "owned.x");
        assert_eq!(owned.as_ref().y, 1.5, "owned.y");

        // Lifetime/destruction contract, made observable by the library.
        assert_eq!(demo_live_count(), 1, "one live handle");
        drop(owned);
        assert_eq!(demo_live_count(), 0, "handle released on drop");

        // `into_raw` leaks the handle back out for manual handoff.
        let raw = OwnedDemoVec2::create(1, 2.0).expect("create").into_raw();
        assert_eq!(demo_live_count(), 1, "raw handle live");
        assert_eq!(demo_vec2_destroy(raw), 0, "manual destroy");
        assert_eq!(demo_live_count(), 0, "manual release");

        // Error boundary: the generated Result wrapper turns the C status code
        // into a typed result; the native exception was caught in the library.
        assert_eq!(demo_div_result(10, 2), Ok(5), "div ok");
        let err = demo_div_result(10, 0).expect_err("div by zero");
        assert_eq!(err.0, 1, "div error status");

        // Runtime metadata: parse the ABIX Metadata Region embedded in this
        // projection and check the facts it records against the Rust type.
        let region = abi::metadata::Region::open(&abi::AMC_METADATA_REGION).expect("metadata region");
        let demo = region.find_type_by_name("DemoVec2").expect("DemoVec2 in metadata");
        assert_eq!(demo.size, 16, "metadata size");
        assert_eq!(demo.align, 8, "metadata align");
        assert_eq!(demo.id.lo, DemoVec2::ABIX_TYPE_ID_LO, "metadata type id lo");
        assert_eq!(demo.id.hi, DemoVec2::ABIX_TYPE_ID_HI, "metadata type id hi");
        let x = region.find_field(demo.id, "x").expect("field x in metadata");
        let y = region.find_field(demo.id, "y").expect("field y in metadata");
        assert_eq!((x.offset, y.offset), (0, 8), "metadata field offsets");
        let add = region.find_function_by_name("demo_add").expect("demo_add in metadata");
        assert_eq!(add.calling_convention, 1, "demo_add is cdecl");

        // Ownership contract (read at run time, never inferred): the factory
        // returns an owned value released by the declared destroy entry point;
        // `demo_scale` only borrows its argument; destroy consumes its handle.
        let create_index = region.function_index_by_name("demo_vec2_create").expect("create index");
        let ret = region.find_ownership(1, create_index).expect("create return contract");
        assert_eq!(ret.mode, abi::metadata::OWNERSHIP_OWNED, "create returns owned");
        assert_eq!(
            ret.release_function,
            region.function_index_by_name("demo_vec2_destroy").expect("destroy index"),
            "release entry point"
        );
        let scale = region.find_function_by_name("demo_scale").expect("demo_scale");
        let borrowed = region.find_ownership(0, scale.parameter_begin).expect("scale param contract");
        assert_eq!(borrowed.mode, abi::metadata::OWNERSHIP_BORROWED, "scale borrows");
        let destroy = region.find_function_by_name("demo_vec2_destroy").expect("destroy");
        let consumed = region.find_ownership(0, destroy.parameter_begin).expect("destroy param contract");
        assert_eq!(consumed.mode, abi::metadata::OWNERSHIP_OWNED, "destroy consumes");

        // The error boundary contract is discoverable from the same directory.
        let div_index = region.function_index_by_name("demo_div").expect("div index");
        let ec = region.find_error(div_index).expect("div error contract");
        assert_eq!(ec.status_source, 0, "div status is the return value");
        assert_eq!(ec.success_value, 0, "div success value");
        assert_eq!(ec.value_parameter, 2, "div value parameter");

        // Target platform verification, read from the same region.
        let target = region.target().expect("target description");
        assert_eq!(target.pointer_bits, 64, "pointer width");
        assert_eq!(target.endianness, 1, "little-endian");
        let expected = abi::metadata::TargetExpectation {
            arch: 1,
            os: 1,
            target_abi: 1,
            pointer_bits: 64,
            endianness: 1,
        };
        assert_eq!(region.verify_target(expected), abi::metadata::TARGET_COMPATIBLE, "target compatible");
        let wrong = abi::metadata::TargetExpectation { arch: 2, ..Default::default() };
        assert_eq!(region.verify_target(wrong), abi::metadata::TARGET_INCOMPATIBLE, "target incompatible");

        println!("interop ok: add=5 scale={scaled} div=5 err=1 types={}", region.header.type_count);
    }
}
