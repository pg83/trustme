#![feature(decl_macro)]
trait Read { type Item; const BASE: u8; fn read(&self) -> Self::Item; }
struct Counter(u8);
macro read_items() {
    type Item = u8;
    const BASE: u8 = 7;
    fn read(&self) -> u8 { self.0 + 7 }
}
impl Read for Counter { read_items!(); }
macro iterator_items() {
    type Item = u8;
    fn next(&mut self) -> Option<u8> {
        if self.0 == 0 { None } else { self.0 -= 1; Some(self.0) }
    }
}
impl Iterator for Counter { iterator_items!(); }
fn main() {
    let mut counter = Counter(3);
    assert_eq!(Read::read(&counter), 10);
    assert_eq!(<Counter as Read>::BASE, 7);
    assert_eq!(counter.next(), Some(2));
    assert_eq!(counter.collect::<Vec<_>>(), [1, 0]);
}
