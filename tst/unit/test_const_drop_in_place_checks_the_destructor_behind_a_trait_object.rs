//@ compile-fail: cannot be evaluated at compile-time
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]
use std::mem::ManuallyDrop;
trait Tagged {}
struct Tick(*mut u32);
impl Tagged for Tick {}
impl const Drop for Tick { fn drop(&mut self) { unsafe { *self.0 += 13; } } }
const fn evaluate() -> u32 {
    let mut total = 3;
    let mut value = ManuallyDrop::new(Tick(&raw mut total));
    let address = &raw mut value as *mut Tick;
    unsafe { std::ptr::drop_in_place(address as *mut dyn Tagged); }
    total
}
const TOTAL: u32 = evaluate();
fn main() { assert_eq!(TOTAL, 16); assert_eq!(evaluate(), 16); }
