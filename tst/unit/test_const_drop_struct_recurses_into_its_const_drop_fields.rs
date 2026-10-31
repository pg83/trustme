#![feature(const_trait_impl, const_destruct)]
struct Guard<'a>(&'a mut u32, u32);
impl const Drop for Guard<'_> { fn drop(&mut self) { *self.0 += self.1; } }
struct Envelope<'a> { first: Guard<'a>, tag: u16, second: Guard<'a> }
const fn evaluate() -> (u32, u32) {
    let mut a = 2; let mut b = 3;
    { let _value = Envelope { first: Guard(&mut a, 5), tag: 37, second: Guard(&mut b, 8) }; }
    (a, b)
}
const TOTALS: (u32, u32) = evaluate();
fn main() { assert_eq!(TOTALS, (7, 11)); assert_eq!(evaluate(), TOTALS); }
