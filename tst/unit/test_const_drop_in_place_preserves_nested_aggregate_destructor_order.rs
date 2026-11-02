//@ compile-variant: -O
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]
use std::mem::ManuallyDrop;
struct Mark(*mut u32, u32);
impl const Drop for Mark { fn drop(&mut self) { unsafe { *self.0 = *self.0 * 10 + self.1; } } }
enum Packet { Empty, Full(Mark) }

const fn evaluate() -> u32 {
    let mut order = 0;
    let pointer = &raw mut order;
    let mut value = ManuallyDrop::new((Mark(pointer, 1), [Mark(pointer, 2), Mark(pointer, 3)], Packet::Full(Mark(pointer, 4))));
    let address = &raw mut value as *mut (Mark, [Mark; 2], Packet);
    unsafe { std::ptr::drop_in_place(address); }
    let mut empty = ManuallyDrop::new(Packet::Empty);
    unsafe { std::ptr::drop_in_place(&raw mut empty as *mut Packet); }
    order
}
const ORDER: u32 = evaluate();
fn main() { assert_eq!(ORDER, 1234); assert_eq!(evaluate(), 1234); }
