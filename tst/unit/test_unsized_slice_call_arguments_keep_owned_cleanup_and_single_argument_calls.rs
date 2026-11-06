#![feature(unsized_fn_params)]
#![allow(incomplete_features)]
use std::cell::Cell;
struct Token<'a>(&'a Cell<u32>, u32);
impl Drop for Token<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}
fn consume(values: [Token<'_>], last: Token<'_>) -> u32 {
    values[0].1 + values[1].1 + last.1
}
fn single(values: [Token<'_>]) -> u32 { values[0].1 }
#[inline(never)]
fn marker(count: &Cell<u32>) -> Token<'_> {
    if std::hint::black_box(false) { panic!("marker construction failed"); }
    Token(count, 7)
}
fn main() {
    let count = Cell::new(0);
    let values: Box<[Token<'_>]> = Box::new([Token(&count, 2), Token(&count, 5)]);
    let function: fn([Token<'_>], Token<'_>) -> u32 = consume;
    assert_eq!(function(*values, marker(&count)), 14);
    assert_eq!(count.get(), 3);
    let value: Box<[Token<'_>]> = Box::new([Token(&count, 11)]);
    assert_eq!(single(*value), 11);
    assert_eq!(count.get(), 4);
}
