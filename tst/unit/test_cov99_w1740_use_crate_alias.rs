//@ proc-macro-aux-build: cov99_w1740_identity.rs

#[cov99_w1740_identity::keep]
use crate as root_alias;

pub const VALUE: u8 = 17;

fn main() {
    assert_eq!(root_alias::VALUE, 17);
}
