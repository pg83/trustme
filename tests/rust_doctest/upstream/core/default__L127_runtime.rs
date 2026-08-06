// Extracted from library/core/src/default.rs:127
#![allow(unused)]
fn main() {
    #[allow(dead_code)]
    enum Kind {
        A,
        B,
        C,
    }
    
    impl Default for Kind {
        fn default() -> Self { Kind::A }
    }
}
