mod values {
    pub const BASE: usize = 7;

    pub fn add(value: usize) -> usize {
        value + BASE
    }
}

pub use values::{add, BASE};

pub mod forwarded {
    pub use super::{add, BASE};
}
