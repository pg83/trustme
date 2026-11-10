//@ edition: 2021
//@ compile-fail: error
#![feature(gen_blocks)]

macro_rules! generate {
    ($keyword:path) => {
        $keyword move { yield 41u8; }
    };
}

fn main() {
    let mut values = generate!(gen);
    assert_eq!(values.next(), Some(41));
    assert_eq!(values.next(), None);
}
