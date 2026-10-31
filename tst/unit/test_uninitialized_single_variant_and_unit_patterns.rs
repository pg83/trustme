use std::cell::Cell;
struct Mark<'a>(u64, &'a Cell<u64>);
impl Drop for Mark<'_> {
    fn drop(&mut self) { self.1.set(self.1.get() * 10 + self.0); }
}
enum Packet<'a> { Only { left: Mark<'a>, right: Mark<'a> } }
struct Unit;
fn main() {
    let log = Cell::new(0);
    {
        let Packet::Only { right, left }: Packet<'_>;
        right = Mark(2, &log);
        left = Mark(1, &log);
        assert_eq!(right.0 + left.0, 3);
        let Unit: Unit;
        let (): ();
    }
    assert_eq!(log.get(), 12);
}
