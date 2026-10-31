use std::cell::Cell;
struct Leaf<'a>(&'a Cell<u32>, u32);
impl Drop for Leaf<'_> { fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); } }
trait Marker { fn id(&self) -> u32; }
impl Marker for Leaf<'_> { fn id(&self) -> u32 { self.1 } }
fn main() {
    let log = Cell::new(0);
    let slice: Box<[Leaf<'_>]> = Box::new([Leaf(&log, 1), Leaf(&log, 2), Leaf(&log, 3)]);
    drop(slice);
    assert_eq!(log.get(), 123);
    let object: Box<dyn Marker + '_> = Box::new(Leaf(&log, 4));
    assert_eq!(object.id(), 4);
    drop(object);
    assert_eq!(log.get(), 1234);
}
