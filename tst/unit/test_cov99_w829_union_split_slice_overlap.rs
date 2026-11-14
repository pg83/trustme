//@ crate-type: lib

#![no_std]

pub union Payload {
    pub bytes: &'static [u8],
    pub flag: bool,
}

pub unsafe fn classify(value: Payload) -> u8 {
    match value {
        Payload { bytes: &[1, ..] } => 1,
        Payload { flag: true } => 2,
        _ => 0,
    }
}
