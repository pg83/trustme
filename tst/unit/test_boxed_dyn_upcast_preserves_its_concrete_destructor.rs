use std::cell::Cell;
struct Leaf<'a>(&'a Cell<u32>, u32);
impl Drop for Leaf<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
trait Base { fn value(&self) -> u32; }
trait Detail: Base { fn extra(&self) -> u32; }
impl Base for Leaf<'_> { fn value(&self) -> u32 { self.1 } }
impl Detail for Leaf<'_> { fn extra(&self) -> u32 { self.1 + 1 } }
fn main() {
    let log = Cell::new(0);
    let object: Box<dyn Detail + '_> = Box::new(Leaf(&log, 37));
    assert_eq!(object.extra(), 38);
    let base: Box<dyn Base + '_> = object;
    assert_eq!(base.value(), 37);
    drop(base);
    assert_eq!(log.get(), 1);
}
