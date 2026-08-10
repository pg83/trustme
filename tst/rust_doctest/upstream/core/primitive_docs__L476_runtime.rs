// Extracted from library/core/src/primitive_docs.rs:476
#![allow(unused)]
fn main() {
    fn returns_i64() -> i64 {
        1i64
    }
    fn returns_unit() {
        1i64;
    }

    let is_i64 = {
        returns_i64()
    };
    let is_unit = {
        returns_i64();
    };
}
