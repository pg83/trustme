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
fn choose(drops: &Cell<u8>, branch: bool, moved: bool) {
    let pair = (Token(drops, 1), Token(drops, 2));
    if branch {
        if moved { drop(pair.0); }
    } else {
        if !moved { drop(pair.0); }
    }
    assert_eq!(drops.get(), if branch == moved { 1 } else { 0 });
}

fn main() {
    for branch in [false, true] {
        for moved in [false, true] {
            let drops = Cell::new(0);
            choose(&drops, std::hint::black_box(branch), std::hint::black_box(moved));
            assert_eq!(drops.get(), 3);
        }
    }
}
