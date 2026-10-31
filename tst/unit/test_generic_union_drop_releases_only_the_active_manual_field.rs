use std::cell::Cell;
use std::mem::ManuallyDrop;
struct Leaf<'a>(&'a Cell<u32>, u32);
impl Drop for Leaf<'_> { fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); } }
union Slot<T> { value: ManuallyDrop<T>, unused: usize }
impl<T> Drop for Slot<T> { fn drop(&mut self) { unsafe { ManuallyDrop::drop(&mut self.value); } } }
fn main() {
    let log = Cell::new(0);
    drop([Slot { value: ManuallyDrop::new(Leaf(&log, 1)) }, Slot { value: ManuallyDrop::new(Leaf(&log, 2)) }]);
    assert_eq!(log.get(), 12);
}
