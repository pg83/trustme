//@ compile-variant: -O
pub extern crate self as first;
pub use first as second;
pub use second as third;

pub mod payload {
    pub mod inner {
        pub struct Number(pub u32);
        pub fn read(value: &Number) -> u32 { value.0 }
    }
}

pub use third::payload as facade;
pub use facade::inner as exposed;

fn main() {
    use third::facade::inner::{Number, read};
    let number = Number(67);
    assert_eq!(read(&number), 67);
    {
        use crate::second as root;
        use root::exposed::*;
        assert_eq!(read(&Number(71)), 71);
    }
}
