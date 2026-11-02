//@ compile-variant: -O
#![feature(core_intrinsics)]
trait Data { const VALUES: *const [u16]; }
struct Table;
impl Data for Table {
    const VALUES: *const [u16] = {
        let view: &[u16] = &[73, 79, 83];
        view as *const [u16]
    };
}
fn main() {
    let selected: *const u16 = unsafe { core::intrinsics::slice_get_unchecked(<Table as Data>::VALUES, 1) };
    assert_eq!(unsafe { *selected }, 79);
}
