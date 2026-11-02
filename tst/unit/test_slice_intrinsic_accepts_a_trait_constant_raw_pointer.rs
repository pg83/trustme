//@ compile-fail: Type mismatch between
#![feature(core_intrinsics)]
trait Data { const VALUES: *const [u16]; }
struct Table;
impl Data for Table { const VALUES: *const [u16] = &[61, 67, 71] as *const [u16]; }
fn main() {
    let selected: *const u16 = unsafe { core::intrinsics::slice_get_unchecked(<Table as Data>::VALUES, 2) };
    assert_eq!(unsafe { *selected }, 71);
}
