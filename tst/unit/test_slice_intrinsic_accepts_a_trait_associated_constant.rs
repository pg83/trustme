//@ compile-variant: -O
#![feature(core_intrinsics)]
trait Data { const VALUES: &'static [u16]; }
struct Table;
impl Data for Table { const VALUES: &'static [u16] = &[31, 43, 59]; }
fn main() {
    let selected: &u16 = unsafe { core::intrinsics::slice_get_unchecked(<Table as Data>::VALUES, 1) };
    assert_eq!(*selected, 43);
}
