//@ aux-build: mutable_owner_graph.rs

use mutable_owner_graph::{nested, Read, Table, Token};

const TOTAL: usize = nested::sum();
const SMALL: &'static [u8; 3] = Table::<3>::values();
const LARGE: &'static [u8; 5] = Table::<5>::values();

fn main() {
    assert_eq!(TOTAL, 26);
    assert_eq!(Token::read(), 20);
    assert_eq!(nested::BYTES, [2, 4, 8]);
    assert_eq!(SMALL, &[3, 3, 3]);
    assert_eq!(LARGE, &[3, 3, 3, 3, 3]);
    assert_eq!(Table::<2>::values(), &[3, 3]);
}
