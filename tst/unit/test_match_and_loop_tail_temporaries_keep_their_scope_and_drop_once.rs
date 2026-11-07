//@ edition: 2021

use std::cell::Cell;

struct Token<'a> {
    drops: &'a Cell<u32>,
    value: u32,
}

impl Drop for Token<'_> {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}

#[inline(never)]
fn inspect(token: &Token<'_>) -> u32 {
    token.value
}

#[inline(never)]
fn exercise(flag: bool, drops: &Cell<u32>) -> u32 {
    let mut result;
    {
        match flag {
            true => {
                result = inspect(&Token { drops, value: 61 });
                inspect(&Token { drops, value: 67 })
            }
            false => {
                result = inspect(&Token { drops, value: 73 });
                inspect(&Token { drops, value: 79 })
            }
        }
    };
    let mut iterations = 0;
    {
        loop {
            if iterations == 2 {
                break;
            }
            iterations += 1;
            { result += inspect(&Token { drops, value: 7 }) }
        }
    };
    result
}

fn main() {
    let drops = Cell::new(0);
    assert_eq!(exercise(std::hint::black_box(true), &drops), 75);
    assert_eq!(drops.get(), 4);
    assert_eq!(exercise(std::hint::black_box(false), &drops), 87);
    assert_eq!(drops.get(), 8);
}
