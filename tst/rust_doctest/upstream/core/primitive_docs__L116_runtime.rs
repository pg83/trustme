// Extracted from library/core/src/primitive_docs.rs:116
#![allow(unused)]
fn main() {
    trait FromStr: Sized {
        type Err;
        fn from_str(s: &str) -> Result<Self, Self::Err>;
    }
}
