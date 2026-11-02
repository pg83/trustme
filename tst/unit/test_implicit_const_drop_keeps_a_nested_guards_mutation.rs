//@ compile-variant: -O
#![feature(const_trait_impl, const_destruct)]
struct Guard<'a>(&'a mut u32);
impl const Drop for Guard<'_> { fn drop(&mut self) { *self.0 += 19; } }
const fn evaluate() -> u32 {
    let mut total = 3;
    { let _guard = Guard(&mut total); }
    total
}
const TOTAL: u32 = evaluate();
fn main() { assert_eq!(TOTAL, 22); assert_eq!(evaluate(), 22); }
