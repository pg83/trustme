//@ compile-variant: -O
use std::cell::Cell;
struct Field<'a> { count: &'a Cell<u8> }
impl Drop for Field<'_> { fn drop(&mut self) { self.count.set(self.count.get() + 1); } }
struct Owner<'a> { field: Field<'a>, count: &'a Cell<u8> }
impl Drop for Owner<'_> { fn drop(&mut self) { self.count.set(self.count.get() + 3); } }
fn main() {
    let count = Cell::new(0);
    drop(Owner { field: Field { count: &count }, count: &count });
    assert_eq!(count.get(), 4);
}
