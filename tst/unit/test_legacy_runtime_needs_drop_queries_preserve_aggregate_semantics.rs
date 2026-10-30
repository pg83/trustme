//@ compile-flags: -Zmir-opt-level=0
#![feature(core_intrinsics)]
#![allow(internal_features)]
struct Token;
impl Drop for Token { fn drop(&mut self) {} }
#[inline(never)]
fn query<T>() -> bool {
    // Modern native rustc requires the nullary intrinsic inside a const block;
    // its public wrapper provides the same query for the native comparison.
    #[cfg(rust_compiler = "trustme")]
    { std::intrinsics::needs_drop::<T>() }
    #[cfg(not(rust_compiler = "trustme"))]
    { std::mem::needs_drop::<T>() }
}
fn main() {
    assert!(query::<Token>());
    assert!(query::<(u8, [Token; 2])>());
    assert!(!query::<[Token; 0]>());
    assert!(!query::<std::mem::ManuallyDrop<Token>>());
    assert!(!query::<&'static Token>());
}
