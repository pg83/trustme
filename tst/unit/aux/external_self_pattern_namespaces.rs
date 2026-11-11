#[derive(PartialEq, Eq)]
pub struct Reading {
    pub value: u8,
}

mod constants {
    pub const BASELINE: super::Reading = super::Reading { value: 17 };
}

pub use constants::BASELINE as Reading;
