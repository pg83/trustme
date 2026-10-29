use std::cell::Cell;
struct Tracked<'a>(&'a Cell<u32>);
impl Drop for Tracked<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}
fn conditional(take: bool) -> u32 {
    let count = Cell::new(0);
    {
        let slot;
        if take {
            slot = Box::new(Tracked(&count));
            drop(*slot);
        }
    }
    count.get()
}
fn main() {
    assert_eq!(conditional(false), 0);
    assert_eq!(conditional(true), 1);
}
