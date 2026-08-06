// Extracted from library/core/src/default.rs:82
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    enum Kind {
        A,
        B,
        C,
    }
    
    impl Default for Kind {
        fn default() -> Self { Kind::A }
    }
}
