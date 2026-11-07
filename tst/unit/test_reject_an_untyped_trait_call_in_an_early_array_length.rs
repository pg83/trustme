//@ compile-fail: error:
trait Width {
    const VALUE: usize;
}
impl<const N: usize> Width for [u8; N] {
    const VALUE: usize = N;
}
const fn unresolved_length() -> usize {
    let value = Default::default();
    let _ = value;
    19
}
const EARLY: usize = <[u8; unresolved_length()] as Width>::VALUE;
fn main() {}
