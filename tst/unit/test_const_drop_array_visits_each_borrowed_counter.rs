#![feature(const_trait_impl, const_destruct)]
struct Guard<'a>(&'a mut u32, u32);
impl const Drop for Guard<'_> { fn drop(&mut self) { *self.0 += self.1; } }
const fn evaluate() -> [u32; 3] {
    let mut totals = [1, 2, 3];
    { let [a, b, c] = &mut totals; let _guards = [Guard(a, 7), Guard(b, 11), Guard(c, 13)]; }
    totals
}
const TOTALS: [u32; 3] = evaluate();
fn main() { assert_eq!(TOTALS, [8, 13, 16]); assert_eq!(evaluate(), TOTALS); }
