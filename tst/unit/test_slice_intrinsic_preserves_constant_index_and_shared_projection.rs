//@ compile-variant: -O
#![feature(core_intrinsics)]
trait Offset { const INDEX: usize; }
struct Second;
impl Offset for Second { const INDEX: usize = 1; }
fn main() {
    let values = [(3u8, 5u8), (7, 11)];
    let slice = &values[..];
    let pair: &(u8, u8) = unsafe { core::intrinsics::slice_get_unchecked(slice, <Second as Offset>::INDEX) };
    assert_eq!(*pair, (7, 11));
}
