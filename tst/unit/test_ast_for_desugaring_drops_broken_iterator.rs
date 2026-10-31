use std::cell::Cell;
struct Iter<'a> { at: u8, calls: &'a Cell<u32>, drops: &'a Cell<u32> }
impl Iterator for Iter<'_> {
    type Item = u8;
    fn next(&mut self) -> Option<u8> {
        self.calls.set(self.calls.get() + 1);
        if self.at == 3 { None } else { let value = self.at; self.at += 1; Some(value) }
    }
}
impl Drop for Iter<'_> { fn drop(&mut self) { self.drops.set(self.drops.get() + 1); } }
fn main() {
    let calls = Cell::new(0);
    let drops = Cell::new(0);
    let mut total = 0;
    for value in (Iter { at: 0, calls: &calls, drops: &drops }) {
        total += value;
        if value == 1 { break; }
    }
    assert_eq!((calls.get(), drops.get()), (2, 1));
    for value in (Iter { at: 0, calls: &calls, drops: &drops }) { total += value; }
    assert_eq!((total, calls.get(), drops.get()), (4, 6, 2));
}
