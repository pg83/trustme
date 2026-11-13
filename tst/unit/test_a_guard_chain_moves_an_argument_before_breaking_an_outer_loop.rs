//@ edition: 2024
#![feature(if_let_guard)]

use core::cell::Cell;

struct Watch<'a>(&'a Cell<u8>);

impl Drop for Watch<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

fn select(value: Watch<'_>, gate: Option<bool>) -> u8 {
    'exit: loop {
        match () {
            () if let Some(ready) = gate && { drop(value); ready } => break 'exit 11,
            _ => break 'exit 13,
        }
    }
}

fn main() {
    for gate in [Some(true), Some(false), None] {
        let drops = Cell::new(0);
        let result = select(Watch(&drops), gate);
        assert_eq!(result, if gate == Some(true) { 11 } else { 13 });
        assert_eq!(drops.get(), 1);
    }
}
