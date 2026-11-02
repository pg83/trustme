//@ compile-variant: -O
#![feature(core_intrinsics)]
trait Data { const VALUES: &'static [u16]; const INDEX: usize; }
struct Table;
impl Data for Table { const VALUES: &'static [u16] = &[89, 97, 101]; const INDEX: usize = 2; }
fn main() {
    let view = std::hint::black_box(<Table as Data>::VALUES);
    let index = std::hint::black_box(<Table as Data>::INDEX);
    let selected: &u16 = unsafe { core::intrinsics::slice_get_unchecked(view, index) };
    assert_eq!(*selected, 101);
}
