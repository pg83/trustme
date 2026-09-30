// time's integration tests pass `0.` through rstest; the literal comes back as
// its text, and a dot with nothing after it ends a float, as in rustc_lexer.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::{echo, literal_from_text};

fn main() {
    assert_eq!(literal_from_text!("0."), 0.0);
    assert_eq!(literal_from_text!("7."), 7.0);
    let x: f64 = echo!(0.);
    assert_eq!(x, 0.0);
}
