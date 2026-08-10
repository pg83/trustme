// Extracted from library/core/src/default.rs:63
#![allow(unused)]
fn main() {
    #[derive(Default)]
    enum Kind {
        #[default]
        A,
        B,
        C,
    }
}
