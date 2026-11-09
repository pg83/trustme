//@ crate-type: lib

pub struct Registry;

fn private_target(value: u8) -> u8 {
    value + 7
}

impl Registry {
    pub const CALLBACK: fn(u8) -> u8 = private_target;
}
