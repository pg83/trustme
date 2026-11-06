#![feature(unsized_fn_params)]
#![allow(incomplete_features)]
use std::cell::Cell;
struct Token<'a>(&'a Cell<u32>, u32);
impl Drop for Token<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}
trait Consume {
    fn consume(self, other: Token<'_>) -> u32;
}
impl Consume for Token<'_> {
    fn consume(self, other: Token<'_>) -> u32 { self.1 + other.1 }
}
#[inline(never)]
fn argument(count: &Cell<u32>, fail: bool) -> Token<'_> {
    if std::hint::black_box(fail) { panic!("argument construction failed"); }
    Token(count, 29)
}
fn main() {
    let count = Cell::new(0);
    let value: Box<dyn Consume + '_> = Box::new(Token(&count, 17));
    assert_eq!(value.consume(argument(&count, false)), 46);
    assert_eq!(count.get(), 2);
}
