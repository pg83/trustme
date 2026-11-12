//@ edition: 2024
#![feature(gen_blocks)]

struct Values;

impl Values {
    gen fn numbers() -> u16 {
        yield 463;
        yield 467;
    }
}

fn main() {
    let mut numbers = Values::numbers();
    assert_eq!(numbers.next(), Some(463));
    assert_eq!(numbers.next(), Some(467));
    assert_eq!(numbers.next(), None);
}
