#![feature(fn_delegation)]
#![allow(incomplete_features)]
trait Read { fn add(&self, delta: u32) -> u32; fn get(&self) -> u32; }
trait Change { fn increment(&mut self, delta: u32); fn double(&mut self); }
trait Take { fn into_value(self) -> u32; }
struct Inner(u32);
impl Read for Inner {
    fn add(&self, delta: u32) -> u32 { self.0 + delta }
    fn get(&self) -> u32 { self.0 }
}
impl Change for Inner {
    fn increment(&mut self, delta: u32) { self.0 += delta; }
    fn double(&mut self) { self.0 *= 2; }
}
impl Take for Inner { fn into_value(self) -> u32 { self.0 } }
struct Wrapped(Inner);
impl Read for Wrapped { reuse <Inner as Read>::* { &self.0 } }
impl Change for Wrapped { reuse <Inner as Change>::* { &mut self.0 } }
impl Take for Wrapped { reuse <Inner as Take>::* { self.0 } }
fn main() {
    let mut value = Wrapped(Inner(31));
    assert_eq!(value.add(4), 35);
    value.increment(8);
    assert_eq!(value.add(3), 42);
    value.double();
    assert_eq!(value.get(), 78);
    assert_eq!(value.into_value(), 78);
}
