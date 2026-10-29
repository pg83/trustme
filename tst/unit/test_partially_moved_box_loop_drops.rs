use std::cell::Cell;
struct Tracked<'a>(&'a Cell<u32>);
impl Drop for Tracked<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}
fn exercise(rounds: u32) -> u32 {
    let count = Cell::new(0);
    {
        let mut slot = Box::new((Tracked(&count), Tracked(&count)));
        drop(slot.0);
        for _ in 0..rounds {
            slot.0 = Tracked(&count);
            drop(slot.0);
        }
        drop(slot.1);
    }
    count.get()
}
fn main() {
    assert_eq!(exercise(0), 2);
    assert_eq!(exercise(3), 5);
}
