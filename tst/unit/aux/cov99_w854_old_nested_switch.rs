#[inline(always)]
pub unsafe fn read(mode: u8) -> u8 {
    match mode {
        0 => match cov99_w854_nested_store::SLOT {
            0 => 7,
            1 => 8,
            _ => 9,
        },
        1 => 10,
        _ => 7,
    }
}
