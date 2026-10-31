//@ compile-fail: Type mismatch
use std::ops::Deref;
struct Layer<T>(T);
impl<T> Deref for Layer<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
fn read(value: &u16) -> u16 { *value }
fn main() { let value = Layer(Layer(41u32)); let _ = read(&value); }
