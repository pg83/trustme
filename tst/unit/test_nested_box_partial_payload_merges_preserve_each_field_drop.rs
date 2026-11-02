//@ compile-variant: -O
use std::cell::Cell;
struct Mark<'a>(u32, &'a Cell<u32>);
impl Drop for Mark<'_> {
    fn drop(&mut self) {
        let bit = 1 << self.0;
        assert_eq!(self.1.get() & bit, 0);
        self.1.set(self.1.get() | bit);
    }
}
fn main() {
    for first in [false, true] {
        for second in [false, true] {
            let bits = Cell::new(0);
            {
                let outer = Box::new(Box::new((Mark(0, &bits), Mark(1, &bits), Mark(2, &bits))));
                if std::hint::black_box(first) { drop((**outer).0); }
                else { drop((**outer).1); }
                if std::hint::black_box(second) { drop((**outer).2); }
            }
            assert_eq!(bits.get(), 7);
        }
    }
}
