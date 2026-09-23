//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// thiserror's `#[error("{}", .0.0)]`: the lexer makes `.` and the float
// literal `0.0` (upstream has no context here), and thiserror splits that
// float's text into two tuple indices. Our lexer's `.`-then-digit path made the
// float without its spelling, the value went out as `0`, and the second
// index was lost - `self.0` where `self.0.0` was written.
use proc_macro_item_passthrough::token_texts;

fn main() {
    assert_eq!(token_texts!(.0.0), ". 0.0");
    assert_eq!(token_texts!(x.0.1), "x . 0.1");
    assert_eq!(token_texts!(.1.25), ". 1.25");
    let t = ((1u8, 2u8), 3u8);
    assert_eq!(t.0.1, 2);
}
