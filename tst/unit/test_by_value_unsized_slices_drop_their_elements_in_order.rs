//@ compile-variant: -O
#![feature(unsized_fn_params)]
#![allow(incomplete_features)]
use std::cell::Cell;
struct Token<'a> { value: u32, order: &'a Cell<u32> }
impl Drop for Token<'_> {
    fn drop(&mut self) { self.order.set(self.order.get() * 10 + self.value); }
}
#[inline(never)]
fn total<'a>(values: [Token<'a>]) -> u32 { values.iter().map(|v| v.value).sum() }
fn main() {
    let order = Cell::new(0);
    let values: Box<[Token<'_>]> = Box::new([
        Token { value: 2, order: &order },
        Token { value: 3, order: &order },
        Token { value: 5, order: &order },
    ]);
    assert_eq!(total(*values), 10);
    assert_eq!(order.get(), 235);
}
