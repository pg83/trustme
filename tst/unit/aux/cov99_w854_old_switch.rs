#[inline(always)]
pub unsafe fn read() -> u8 {
    match cov99_w854_store::VALUE {
        0 => 7,
        1 => 8,
        _ => 9,
    }
}
