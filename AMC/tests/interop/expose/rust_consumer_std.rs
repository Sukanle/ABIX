// Rust consumer exercising the automatic ABIX-STD contract mapping: the exposed
// methods speak SequenceView / StringView / Result, and the contracts are
// recorded in the metadata region.
#[path = "gen/amc_generated.rs"]
mod abi;
use abi::*;

fn main() {
    unsafe {
        let metrics = OwnedMetrics::create().expect("create");

        let data = [1i32, 2, 3, 4];
        let span = abix_span_i32 { data: data.as_ptr(), len: data.len() as u64 };
        assert_eq!(abix_Metrics_sum(metrics.as_ptr(), span), 10, "sequence view");

        let text = b"hello";
        let s = abix_str_utf8 {
            data: text.as_ptr() as *const core::ffi::c_char,
            len: text.len() as u64,
            encoding: 0,
        };
        assert_eq!(abix_Metrics_text_length(metrics.as_ptr(), s), 5, "string view");

        let ok = abix_Metrics_divide(metrics.as_ptr(), 10, 2);
        assert_eq!((ok.status, ok.value), (0, 5), "result ok");
        let err = abix_Metrics_divide(metrics.as_ptr(), 10, 0);
        assert_eq!(err.status, 1, "result error");

        // The `[[error]]` semantics of an `abix_result_*` return are bound
        // automatically: the backend turns the aggregate into a typed Result.
        assert_eq!(abix_Metrics_divide_result(metrics.as_ptr(), 10, 2), Ok(5), "typed result ok");
        let typed = abix_Metrics_divide_result(metrics.as_ptr(), 10, 0);
        assert!(typed.is_err(), "typed result error");
        assert_eq!(typed.unwrap_err().0, 1, "typed error status");

        let region = abi::metadata::Region::open(&abi::AMC_METADATA_REGION).expect("region");
        assert!(region.lowering_count() >= 3, "contracts recorded: {}", region.lowering_count());
    }
    println!("expose std: OK");
}
