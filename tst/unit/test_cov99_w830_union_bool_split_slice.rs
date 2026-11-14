//@ crate-type: lib
//@ compile-flags: -Zstop-after=mir

pub union PatternInput {
    pub flag: bool,
    pub bytes: &'static [u8],
}

pub unsafe fn classify(value: PatternInput) -> u8 {
    match value {
        PatternInput { flag: false } => 0,
        PatternInput { bytes: &[1, ..] } => 1,
        _ => 2,
    }
}
