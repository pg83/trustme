//@ compile-fail: type annotations needed
#![feature(core_intrinsics)]
trait Data { const VALUES: &'static [u8]; }
struct Table<const N: u8>;
impl<const N: u8> Data for Table<N> { const VALUES: &'static [u8] = &[N, N + 1]; }
fn selected<const N: u8>() -> u8 {
    unsafe { *core::intrinsics::slice_get_unchecked(<Table<N> as Data>::VALUES, 1) }
}
fn main() {
    assert_eq!(selected::<13>(), 14);
    assert_eq!(selected::<29>(), 30);
    assert_eq!(selected::<13>(), 14);
}
