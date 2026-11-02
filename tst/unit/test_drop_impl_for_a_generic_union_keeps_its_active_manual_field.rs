//@ compile-variant: -O
use std::cell::Cell;
use std::mem::ManuallyDrop;
struct Field<'a>(&'a Cell<u8>);
impl Drop for Field<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
union Storage<T> { value: ManuallyDrop<T>, empty: () }
impl<T> Drop for Storage<T> { fn drop(&mut self) { unsafe { ManuallyDrop::drop(&mut self.value); } } }
fn main() {
    let count = Cell::new(0);
    drop(Storage { value: ManuallyDrop::new(Field(&count)) });
    assert_eq!(count.get(), 1);
}
