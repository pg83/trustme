//@ compile-fail: error:
trait FixedWidth {
    const VALUE: usize;
}
impl FixedWidth for [u8; 23] {
    const VALUE: usize = 23;
}
const fn unresolved_receiver_length() -> usize {
    let value = Default::default();
    let _ = value;
    23
}
const EARLY: usize = <[u8; unresolved_receiver_length()]>::VALUE;
fn main() {}
