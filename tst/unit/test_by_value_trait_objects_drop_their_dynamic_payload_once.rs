#![feature(unsized_fn_params)]
#![allow(incomplete_features)]
use std::cell::Cell;
struct Token<'a> { value: u32, order: &'a Cell<u32> }
impl Drop for Token<'_> {
    fn drop(&mut self) { self.order.set(self.order.get() * 10 + self.value); }
}
trait Read { fn read(&self) -> u32; }
impl Read for Token<'_> { fn read(&self) -> u32 { self.value } }
#[inline(never)]
fn consume<'a>(value: dyn Read + 'a) -> u32 { value.read() + 13 }
fn main() {
    let order = Cell::new(0);
    let value: Box<dyn Read + '_> = Box::new(Token { value: 7, order: &order });
    assert_eq!(consume(*value), 20);
    assert_eq!(order.get(), 7);
}
