use std::ops::{Deref, DerefMut};
struct Wrap<T>(Box<T>);
impl<T> Deref for Wrap<T> { type Target = Box<T>; fn deref(&self) -> &Box<T> { &self.0 } }
impl<T> DerefMut for Wrap<T> { fn deref_mut(&mut self) -> &mut Box<T> { &mut self.0 } }
fn wrap<T>(value: T) -> Wrap<T> { Wrap(Box::new(value)) }
fn read(value: &u32) -> u32 { *value }
fn bump(value: &mut u32) -> u32 { *value += 1; *value }
fn main() {
    assert_eq!(read(&wrap(17u32)), 17);
    assert_eq!(bump(&mut wrap(41u32)), 42);
}
