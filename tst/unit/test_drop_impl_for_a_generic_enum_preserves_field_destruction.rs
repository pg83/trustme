//@ compile-variant: -O
use std::cell::Cell;
struct Field<'a>(&'a Cell<u8>);
impl Drop for Field<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
enum Choice<T> { Value(T), Empty }
impl<T> Drop for Choice<T> { fn drop(&mut self) {} }
fn main() {
    let count = Cell::new(0);
    drop(Choice::Value(Field(&count)));
    drop(Choice::<Field<'_>>::Empty);
    assert_eq!(count.get(), 1);
}
