pub use bridge::Mirror as Chosen;

mod bridge {
    pub use crate::source::{self as Mirror};
}

pub mod source {
    pub const VALUE: u8 = 11;
}

pub fn source() -> u8 {
    29
}

#[allow(non_snake_case)]
fn Chosen() -> u8 {
    37
}

fn main() {
    assert_eq!(Chosen::VALUE, 11);
    assert_eq!(Chosen(), 37);
    assert_eq!(source(), 29);
}
