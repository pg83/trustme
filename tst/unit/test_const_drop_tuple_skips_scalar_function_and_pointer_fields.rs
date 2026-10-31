#![feature(const_trait_impl, const_destruct)]
struct Guard<'a>(&'a mut u32);
impl const Drop for Guard<'_> { fn drop(&mut self) { *self.0 += 7; } }
fn marker() -> u32 { 41 }
const fn evaluate() -> u32 {
    let mut total = 30;
    { let _tuple = (37u8, marker as fn() -> u32, std::ptr::null::<u8>(), Guard(&mut total), false); }
    total
}
const TOTAL: u32 = evaluate();
fn main() { assert_eq!(TOTAL, 37); assert_eq!(evaluate(), TOTAL); }
