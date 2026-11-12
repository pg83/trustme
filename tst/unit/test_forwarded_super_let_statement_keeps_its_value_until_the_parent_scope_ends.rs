#![feature(super_let)]

use std::cell::Cell;

struct Mark<'a> {
    drops: &'a Cell<u16>,
    value: u16,
}

impl Drop for Mark<'_> {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}

macro_rules! pass_statement {
    ($statement:stmt) => {
        $statement;
    };
}

fn main() {
    let drops = Cell::new(0u16);
    {
        let reference = {
            pass_statement!(super let marker = Mark { drops: &drops, value: 1171 });
            &marker.value
        };
        assert_eq!(*reference, 1171);
        assert_eq!(drops.get(), 0);
    }
    assert_eq!(drops.get(), 1);
}
