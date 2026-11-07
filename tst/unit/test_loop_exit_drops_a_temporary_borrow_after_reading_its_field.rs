use std::cell::Cell;

struct Token<'a> {
    drops: &'a Cell<u16>,
    value: u16,
}

impl Drop for Token<'_> {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}

#[inline(never)]
fn choose(first: bool, drops: &Cell<u16>) -> u16 {
    let before = drops.get();
    let value = loop {
        if first {
            break (&Token { drops, value: 491 }).value;
        }
        break (&Token { drops, value: 499 }).value
    };
    assert_eq!(drops.get(), before + 1);
    value
}

fn main() {
    let drops = Cell::new(0);
    assert_eq!(choose(std::hint::black_box(true), &drops), 491);
    assert_eq!(choose(std::hint::black_box(false), &drops), 499);
    assert_eq!(drops.get(), 2);
}
