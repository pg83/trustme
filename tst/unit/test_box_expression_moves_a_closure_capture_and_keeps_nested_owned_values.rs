//@ compile-flags: -Zdump-hir
#![feature(box_syntax)]

use std::{cell::Cell, rc::Rc};

struct Token {
    drops: Rc<Cell<u32>>,
    value: u32,
}

impl Drop for Token {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}

fn main() {
    let drops = Rc::new(Cell::new(0));
    let owned = Token { drops: drops.clone(), value: 61 };
    let allocate = move || box owned;
    let mut boxed = allocate();
    assert_eq!(boxed.value, 61);
    assert_eq!(drops.get(), 0);
    boxed.value = 67;
    assert_eq!(boxed.value, 67);
    drop(boxed);
    assert_eq!(drops.get(), 1);

    let nested = box (box 83u32);
    assert_eq!(**nested, 83);
    drop(nested);
    assert_eq!(drops.get(), 1);
}
