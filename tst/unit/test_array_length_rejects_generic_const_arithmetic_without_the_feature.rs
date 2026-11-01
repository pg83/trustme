//@ compile-fail: generic parameters may not be used in const operations
fn length<const N: usize>() -> usize {
    core::mem::size_of::<[u8; N + 1]>()
}
fn main() { let _ = length::<2>(); }
