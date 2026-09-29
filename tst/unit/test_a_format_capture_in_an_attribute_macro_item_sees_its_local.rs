// tokio-stream's `#[tokio::test] async fn test_iter_coop_budget` writes
// `for i in 0..limit { assert!(.., "Should be ready at index {i}"); }`. The
// item reaches the macro partly as a re-print of its nodes, and a re-printed
// token has no context of its own: rustc makes such a token at the call site
// (`TokenStream::from_str` is `Span::call_site()`). A re-printed name was sent
// at the call site, a re-printed string literal kept the scope of the lexer
// that read the re-print, so `{i}` captured from it did not see the `i` bound
// beside it once the call site had a context.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use proc_macro_item_passthrough::echo_item;

#[echo_item]
fn captured() -> String {
    let mut out = String::new();
    for i in 0..2 {
        out += &format!("{i}");
        assert!(i < 2, "index {i}");
    }
    out
}

fn main() {
    assert_eq!(captured(), "01");
}
