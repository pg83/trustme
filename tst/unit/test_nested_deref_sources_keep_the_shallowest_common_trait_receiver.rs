//@ compile-fail: error
use std::ops::Deref;
struct Tower<T>(T);
impl<T> Deref for Tower<T> {
    type Target = T;
    fn deref(&self) -> &T { &self.0 }
}
trait Read { fn read(&self) -> u16; }
impl Read for u16 { fn read(&self) -> u16 { *self } }
impl<T: Read> Read for Tower<T> {
    fn read(&self) -> u16 { self.0.read() + 100 }
}
fn pair<T: Read>(values: [&T; 2]) -> u16 { values[0].read() + values[1].read() }
fn single<T: Read>(values: [&T; 1]) -> u16 { values[0].read() }
fn main() {
    let deep = Tower(Tower(5u16));
    let shallow = Tower(7u16);
    assert_eq!(pair([&deep, &shallow]), 212);
    assert_eq!(pair([&shallow, &deep]), 212);
    assert_eq!(single([&deep]), 205);
    assert_eq!(single([&shallow]), 107);
}
