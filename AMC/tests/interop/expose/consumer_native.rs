// Rust consumer of a producer whose public methods take native STL types. The
// declared [[adapt]] mappings turned them into ABIX-STD boundary parameters, so
// the consumer passes a StringView / SequenceView and the generated adapter
// marshals it back onto std::string / std::vector / std::span in the producer.
#[path = "gen/amc_generated.rs"]
mod abi;
use abi::*;

fn text_view(bytes: &[u8]) -> abix_str_utf8 {
    abix_str_utf8 {
        data: bytes.as_ptr() as *const core::ffi::c_char,
        len: bytes.len() as u64,
        encoding: 0,
    }
}

fn main() {
    unsafe {
        let corpus = OwnedCorpus::create().expect("create");
        assert_eq!(abix_Corpus_add_line(corpus.as_ptr(), text_view(b"one")), 1, "string -> StringView");
        assert_eq!(abix_Corpus_add_line(corpus.as_ptr(), text_view(b"two")), 2, "string -> StringView");
        assert_eq!(abix_Corpus_measure(corpus.as_ptr(), text_view(b"hello")), 5, "string_view -> StringView");

        let data = [1i32, 2, 3, 4];
        let span = abix_span_i32 { data: data.as_ptr(), len: data.len() as u64 };
        assert_eq!(abix_Corpus_sum(corpus.as_ptr(), span), 10, "vector -> SequenceView");
        let span2 = abix_span_i32 { data: data.as_ptr(), len: data.len() as u64 };
        assert_eq!(abix_Corpus_sum_span(corpus.as_ptr(), span2), 10, "span -> SequenceView");
    }
    println!("expose native: OK");
}
