#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
#![allow(dead_code)]

trait HasLength {
    const LENGTH: usize;
}

trait Array: HasLength {
    fn array(self) -> [u8; Self::LENGTH];
}

impl HasLength for u8 {
    const LENGTH: usize = 1;
}

impl Array for u8 {
    fn array(self) -> [u8; Self::LENGTH] {
        [0; 1]
    }
}

fn main() {}
