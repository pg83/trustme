//@ compile-variant: -O
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]
use std::mem::ManuallyDrop;
struct Zero;
impl const Drop for Zero { fn drop(&mut self) {} }

const fn evaluate() -> usize {
    let mut empty = ManuallyDrop::new([] as [Zero; 0]);
    let mut full = ManuallyDrop::new([Zero, Zero, Zero]);
    unsafe {
        std::ptr::drop_in_place((&raw mut empty as *mut [Zero; 0]) as *mut [Zero]);
        std::ptr::drop_in_place((&raw mut full as *mut [Zero; 3]) as *mut [Zero]);
        std::ptr::drop_in_place(std::ptr::dangling_mut::<()>());
    }
    core::mem::size_of::<Zero>()
}
const SIZE: usize = evaluate();
fn main() { assert_eq!(SIZE, 0); assert_eq!(evaluate(), 0); }
