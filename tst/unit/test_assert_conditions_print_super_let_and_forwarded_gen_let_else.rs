//@ compile-fail: right curly brace before else in a let-else initializer requires parentheses
//@ edition: 2024
#![feature(super_let, gen_blocks)]

macro_rules! check_generator {
    ($initializer:expr) => {
        assert!({
            let mut stream = $initializer else {
                unreachable!()
            };
            stream.next() == Some(1117u16) && stream.next().is_none()
        });
    };
}

fn main() {
    assert!({
        super let value = 1111u16;
        value == 1111
    });
    check_generator!(gen {
        yield 1117u16;
    });
}
