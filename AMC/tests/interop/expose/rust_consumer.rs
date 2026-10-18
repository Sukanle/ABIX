#[path = "gen/amc_generated.rs"]
mod abi;
use abi::*;

fn main() {
    unsafe {
        let counter = OwnedCounter::create(40).expect("create");
        assert_eq!(abix_Counter_get(counter.as_ptr()), 40);
        abix_Counter_add(counter.as_ptr(), 2);
        assert_eq!(abix_Counter_get(counter.as_ptr()), 42);
    }
    println!("expose counter: OK");
}
