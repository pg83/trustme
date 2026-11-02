//@ compile-variant: -O
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]
use std::mem::ManuallyDrop;

struct Tick(*mut u32);
impl const Drop for Tick {
    fn drop(&mut self) { unsafe { *self.0 += 7; } }
}

const fn evaluate() -> u32 {
    let mut total = 1;
    let pointer = &raw mut total;
    let mut values = ManuallyDrop::new([Tick(pointer), Tick(pointer)]);
    let array = &raw mut values as *mut [Tick; 2];
    unsafe { std::ptr::drop_in_place(array as *mut [Tick]); }
    total
}

const TOTAL: u32 = evaluate();
fn main() { assert_eq!(TOTAL, 15); assert_eq!(evaluate(), 15); }
