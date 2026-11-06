//@ default-compile-flags: -O
#![feature(unsized_fn_params)]
#![allow(incomplete_features)]
use std::cell::Cell;
struct Token<'a>(&'a Cell<u32>, u32);
impl Drop for Token<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}
struct Bundle<'a, T: ?Sized> { head: Token<'a>, tail: T }
fn make<'a>(count: &'a Cell<u32>, order: &Cell<u32>) -> Box<Bundle<'a, [Token<'a>]>> {
    order.set(1);
    Box::new(Bundle { head: Token(count, 2), tail: [Token(count, 3), Token(count, 5)] })
}
fn marker<'a>(count: &'a Cell<u32>, order: &Cell<u32>, digit: u32, value: u32) -> Token<'a> {
    order.set(order.get() * 10 + digit);
    Token(count, value)
}
fn consume(value: Bundle<'_, [Token<'_>]>, left: Token<'_>, right: Token<'_>) -> u32 {
    value.head.1 + value.tail[1].1 + left.1 + right.1
}
fn main() {
    let count = Cell::new(0);
    let order = Cell::new(0);
    assert_eq!(consume(*make(&count, &order), marker(&count, &order, 2, 7), marker(&count, &order, 3, 11)), 25);
    assert_eq!(order.get(), 123);
    assert_eq!(count.get(), 5);
}
