//@ compile-fail: generic parameters
struct Limit<const N: usize>;
impl<const N: usize> Limit<N> { const VALUE: usize = N; }
fn matches<const N: usize>(value: usize) -> bool {
    match value { Limit::<N>::VALUE => true, _ => false }
}
fn main() { let _ = matches::<13>(13); }
