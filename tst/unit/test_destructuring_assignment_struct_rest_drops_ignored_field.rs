use std::cell::Cell;
struct CountDrop<'a>(&'a Cell<u32>);
impl Drop for CountDrop<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
struct Packet<'a> { head: u32, ignored: CountDrop<'a> }
fn main() {
    let count = Cell::new(0);
    let mut head = 0;
    Packet { head, .. } = Packet { head: 17, ignored: CountDrop(&count) };
    assert_eq!((head, count.get()), (17, 1));
}
