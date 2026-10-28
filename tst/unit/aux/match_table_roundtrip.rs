#[inline]
pub fn signed(value: i32) -> u8 {
    match value { -17 => 1, 6 => 2, 1025 => 3, _ => 0 }
}

#[inline]
pub fn unsigned(value: u64) -> u8 {
    match value { 2 => 4, 0x1000_0000_0001 => 5, u64::MAX => 6, _ => 0 }
}

#[inline]
pub fn text(value: &str) -> u8 {
    match value { "left" => 7, "right" => 8, "" => 9, _ => 0 }
}
