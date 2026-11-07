//@ compile-flags: -Cpanic=unwind -Zmir-opt-level=3

use std::cell::Cell;

struct Token<'a> {
    drops: &'a Cell<u16>,
    word: u16,
}

impl Token<'_> {
    #[inline(always)]
    fn read(&self) -> u16 {
        self.word
    }
}

impl Drop for Token<'_> {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}

#[inline(always)]
fn invoke<F: FnOnce() -> u16>(function: F) -> u16 {
    function()
}

fn main() {
    let drops = Cell::new(0);
    let direct = Token { drops: &drops, word: 109 };
    assert_eq!(invoke(move || direct.read() + 1), 110);
    assert_eq!(drops.get(), 1);

    let erased = Token { drops: &drops, word: 113 };
    let boxed: Box<dyn FnOnce() -> u16 + '_> = Box::new(move || erased.read() + 2);
    assert_eq!(boxed(), 115);
    assert_eq!(drops.get(), 2);
}
