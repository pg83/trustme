//@ compile-variant: -O
use std::cell::Cell;
struct Mark<'a>(usize, &'a Cell<u64>);
impl Drop for Mark<'_> {
    fn drop(&mut self) {
        let bit = 1u64 << self.0;
        assert_eq!(self.1.get() & bit, 0);
        self.1.set(self.1.get() | bit);
    }
}
macro_rules! check {
    ($length:literal) => {
        for first in [false, true] {
            let bits = Cell::new(0);
            {
                let values: [Mark<'_>; $length] = std::array::from_fn(|index| Mark(index, &bits));
                if std::hint::black_box(first) { let [head, ..] = values; drop(head); }
                else { let [.., last] = values; drop(last); }
            }
            assert_eq!(bits.get(), (1u64 << $length) - 1);
        }
    };
}
fn main() { check!(31); check!(32); check!(33); }
