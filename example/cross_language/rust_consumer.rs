// Rust consumer for the cross-language example.
//
// `amc generate -l rust` writes `gen/amc_generated.rs`; this file only uses the
// generated `repr(C)` type, raw C entry points and the ownership wrapper. The
// generated size/align/offset assertions run at compile time.
#[path = "gen/amc_generated.rs"]
mod abi;

use abi::*;

fn main() {
    unsafe {
        let sum = shape_add(ShapeVec { x: 1.0, y: 2.0 }, ShapeVec { x: 3.0, y: 4.0 });
        assert_eq!((sum.x, sum.y), (4.0, 6.0), "shape_add");

        let owned = OwnedShapeVec::create(3.0, 4.0).expect("shape_create");
        assert_eq!(shape_live_count(), 1, "one live handle");
        assert_eq!(owned.as_ref().x, 3.0, "owned.x");
        drop(owned);
        assert_eq!(shape_live_count(), 0, "destroy through the owner wrapper");

        let mut input = ShapeVec { x: 2.0, y: 5.0 };
        let mut output = ShapeVec { x: 0.0, y: 0.0 };
        assert_eq!(shape_scale(&mut input, 2.0, &mut output), 0, "shape_scale status");
        assert_eq!((output.x, output.y), (4.0, 10.0), "shape_scale result");
        assert_ne!(shape_scale(std::ptr::null_mut(), 2.0, &mut output), 0, "shape_scale error");

        let mut quotient = 0.0;
        assert_eq!(shape_div(9.0, 3.0, &mut quotient), 0, "shape_div status");
        assert_eq!(quotient, 3.0, "shape_div result");
        assert_ne!(shape_div(1.0, 0.0, &mut quotient), 0, "shape_div error");

        println!("rust example ok");
    }
}
