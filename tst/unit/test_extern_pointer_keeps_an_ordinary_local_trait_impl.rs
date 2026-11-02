//@ compile-variant: -O
#![feature(extern_types)]
unsafe extern "C" { type Foreign; }
trait Describe { fn width(&self) -> usize; }
impl Describe for *const Foreign { fn width(&self) -> usize { core::mem::size_of_val(self) } }
fn main() {
    let pointer: *const Foreign = core::ptr::null();
    assert_eq!(pointer.width(), core::mem::size_of::<usize>());
}
