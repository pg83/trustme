//@ compile-variant: -O
use std::cell::Cell;
use std::mem::ManuallyDrop;
struct Mark<'a>(&'a Cell<u8>);
impl Drop for Mark<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
union Choice<'a> { mark: ManuallyDrop<Mark<'a>>, empty: () }
impl Drop for Choice<'_> { fn drop(&mut self) { unsafe { ManuallyDrop::drop(&mut self.mark); } } }
fn main() {
    let count = Cell::new(0);
    { let choice = Choice { mark: ManuallyDrop::new(Mark(&count)) }; drop(choice); }
    assert_eq!(count.get(), 1);
}
