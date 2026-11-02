//@ compile-variant: -O
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]
use std::mem::ManuallyDrop;
struct Tick(*mut u32);
impl const Drop for Tick { fn drop(&mut self) { unsafe { *self.0 += 7; } } }
struct Batch<T: ?Sized> { head: Tick, tail: T }

const fn evaluate() -> u32 {
    let mut total = 1;
    let pointer = &raw mut total;
    let mut batch = ManuallyDrop::new(Batch { head: Tick(pointer), tail: [Tick(pointer), Tick(pointer)] });
    let sized = &raw mut batch as *mut Batch<[Tick; 2]>;
    unsafe { std::ptr::drop_in_place(sized as *mut Batch<[Tick]>); }
    total
}
const TOTAL: u32 = evaluate();
fn main() { assert_eq!(TOTAL, 22); assert_eq!(evaluate(), 22); }
