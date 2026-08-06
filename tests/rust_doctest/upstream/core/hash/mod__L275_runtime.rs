// Extracted from library/core/src/hash/mod.rs:275
#![allow(unused)]
fn main() {
    fn foo(hasher: &mut impl std::hash::Hasher) {
    hasher.write(&[1, 2]);
    hasher.write(&[3, 4, 5, 6]);
    }
}
