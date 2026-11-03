#![feature(unsized_fn_params)]
#![allow(incomplete_features)]
use std::cell::Cell;
struct Token<'a> { value: u32, order: &'a Cell<u32> }
impl Drop for Token<'_> {
    fn drop(&mut self) { self.order.set(self.order.get() * 10 + self.value); }
}
#[repr(C)]
struct Packet<'a, T: ?Sized> { prefix: Token<'a>, body: T }
#[inline(never)]
fn consume<'a>(value: Packet<'a, [Token<'a>]>) -> u32 {
    value.prefix.value + value.body.iter().map(|v| v.value).sum::<u32>()
}
fn main() {
    let order = Cell::new(0);
    let value: Box<Packet<'_, [Token<'_>]>> = Box::new(Packet {
        prefix: Token { value: 2, order: &order },
        body: [Token { value: 5, order: &order }, Token { value: 7, order: &order }],
    });
    assert_eq!(consume(*value), 14);
    assert_eq!(order.get(), 257);
}
