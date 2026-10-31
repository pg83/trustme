use std::cell::Cell;
struct Leaf<'a>(&'a Cell<u32>);
impl Drop for Leaf<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
trait Probe { fn value(&self) -> u32 { 37 } }
impl<T: ?Sized> Probe for T {}
fn main() {
    let log = Cell::new(0);
    let mut leaf = Leaf(&log);
    let erased: Box<dyn Probe + '_> = Box::new(&mut leaf);
    assert_eq!(erased.as_ref().value(), 37);
    drop(erased);
    assert_eq!(log.get(), 0);
    drop(leaf);
    assert_eq!(log.get(), 1);
}
