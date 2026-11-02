//@ compile-variant: -O
use std::cell::Cell;

struct Token<'a>(&'a Cell<u8>, u8);
impl Drop for Token<'_> {
    fn drop(&mut self) {
        assert_eq!(self.0.get() & self.1, 0);
        self.0.set(self.0.get() | self.1);
    }
}

#[inline(never)]
fn choose(drops: &Cell<u8>, first: bool, second: bool) {
    let tokens = (Token(drops, 1), Token(drops, 2), Token(drops, 4));
    'scope: loop {
        if first { drop(tokens.0); break 'scope; }
        if second { drop(tokens); return; }
        drop(tokens.1);
        break;
    }
}

fn main() {
    for first in [false, true] {
        for second in [false, true] {
            let drops = Cell::new(0);
            choose(&drops, std::hint::black_box(first), std::hint::black_box(second));
            assert_eq!(drops.get(), 7);
        }
    }
}
