#![feature(const_trait_impl, const_destruct)]
struct Guard<'a>(&'a mut u32);
impl const Drop for Guard<'_> { fn drop(&mut self) { *self.0 += 7; } }
enum Choice<'a> { Empty, Held(Guard<'a>, u16) }
const fn evaluate(held: bool) -> u32 {
    let mut total = 30;
    { let _value = if held { Choice::Held(Guard(&mut total), 41) } else { Choice::Empty }; }
    total
}
const TOTALS: (u32, u32) = (evaluate(false), evaluate(true));
fn main() { assert_eq!(TOTALS, (30, 37)); assert_eq!((evaluate(false), evaluate(true)), TOTALS); }
