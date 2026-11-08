//@ aux-build: const_match_dispatch.rs
extern crate const_match_dispatch as wire;
const EMPTY: u16 = wire::classify(wire::Choice::Empty);
const BYTE: u16 = wire::classify(wire::Choice::Byte(7));
const PAIR: u16 = wire::classify(wire::Choice::Pair(3, 8));
const SMALL: u16 = wire::sparse(1);
const LARGE: u16 = wire::sparse(4096);
const FALLBACK: u16 = wire::sparse(0);
fn main() {
    assert_eq!([EMPTY, BYTE, PAIR, SMALL, LARGE, FALLBACK], [0, 7, 11, 9, 23, 5]);
    assert_eq!(wire::classify(wire::Choice::Pair(3, 8)), PAIR);
    assert_eq!(wire::sparse(17), 11);
}
