//@ compile-variant: -O
#![feature(core_intrinsics)]
trait Data { const VALUES: &'static [()]; }
struct Table;
impl Data for Table { const VALUES: &'static [()] = &[(); 4]; }
fn main() {
    let index = std::hint::black_box(3usize);
    let selected: &() = unsafe { core::intrinsics::slice_get_unchecked(<Table as Data>::VALUES, index) };
    assert_eq!(*selected, ());
}
