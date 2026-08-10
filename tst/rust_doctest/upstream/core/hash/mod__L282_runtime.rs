// Extracted from library/core/src/hash/mod.rs:282
#![allow(unused)]
fn main() {
    fn foo(hasher: &mut impl std::hash::Hasher) {
    hasher.write(&[1, 2, 3, 4]);
    hasher.write(&[5, 6]);
    }
}
