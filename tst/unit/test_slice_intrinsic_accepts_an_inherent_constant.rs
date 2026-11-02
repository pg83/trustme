//@ compile-variant: -O
#![feature(core_intrinsics)]
struct Table;
impl Table { const VALUES: &'static [i32] = &[-13, 17, -29]; }
fn main() {
    let selected: &i32 = unsafe { core::intrinsics::slice_get_unchecked(Table::VALUES, 2) };
    assert_eq!(*selected, -29);
}
