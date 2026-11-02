//@ compile-fail: panicked
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]
use std::mem::ManuallyDrop;
struct Zero;
impl const Drop for Zero { fn drop(&mut self) { panic!("zero-sized element was dropped"); } }
const CHECK: () = {
    let mut values = ManuallyDrop::new([Zero, Zero]);
    unsafe { std::ptr::drop_in_place((&raw mut values as *mut [Zero; 2]) as *mut [Zero]); }
};
fn main() { let _ = CHECK; }
