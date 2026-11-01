#![feature(const_trait_impl, const_destruct)]
struct Guard<'a>(&'a mut u16, u16);
impl const Drop for Guard<'_> { fn drop(&mut self) { *self.0 += self.1; } }
enum Slot<'a> { Empty, Full(Guard<'a>) }
const fn count() -> u16 {
    let mut result = 0;
    let mut slot = Slot::Full(Guard(&mut result, 7));
    match slot { Slot::Full(_guard) => {}, Slot::Empty => {} }
    slot = Slot::Empty;
    { let _finished = slot; }
    result
}
const VALUE: u16 = count();
fn main() { assert_eq!(VALUE, 7); }
