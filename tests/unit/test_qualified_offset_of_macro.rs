//@ edition: 2015

struct Pair {
    first: u8,
    second: u16,
}

fn main() {
    assert_eq!(core::mem::offset_of!(Pair, second), 2);
}
