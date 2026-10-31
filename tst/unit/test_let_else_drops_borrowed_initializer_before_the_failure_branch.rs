use std::cell::Cell;
struct Source<'a> { value: Option<u8>, drops: &'a Cell<u8> }
impl Drop for Source<'_> { fn drop(&mut self) { self.drops.set(self.drops.get() + 1); } }
fn select(present: bool, drops: &Cell<u8>) -> u8 {
    let Some(ref value) = Source { value: present.then_some(37), drops }.value else {
        assert_eq!(drops.get(), 1);
        return 17;
    };
    assert_eq!(drops.get(), 0);
    *value
}
fn main() {
    let drops = Cell::new(0);
    assert_eq!(select(true, &drops), 37);
    assert_eq!(drops.get(), 1);
    drops.set(0);
    assert_eq!(select(false, &drops), 17);
    assert_eq!(drops.get(), 1);
}
