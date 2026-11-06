#[inline]
pub fn unsigned(value: u64) -> u8 {
    match value { 0 => 11, 17 => 13, 140737488355328 => 19, _ => 23 }
}
#[inline]
pub fn signed(value: i64) -> u8 {
    match value { -31 => 29, 0 => 31, 549755813888 => 37, _ => 41 }
}
