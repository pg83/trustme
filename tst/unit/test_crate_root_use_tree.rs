mod imported {
    pub const VALUE: u32 = 42;
}

fn main() {
    use crate::{imported as crate_import};

    assert_eq!(crate_import::VALUE, 42);
}
