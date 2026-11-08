#[derive(Clone, Copy)]
pub enum Choice { Empty, Byte(u8), Pair(u8, u8) }
#[inline(never)]
pub const fn classify(value: Choice) -> u16 {
    match value {
        Choice::Empty => 0,
        Choice::Byte(value) => value as u16,
        Choice::Pair(left, right) => left as u16 + right as u16,
    }
}
#[inline(never)]
pub const fn sparse(value: u16) -> u16 {
    match value { 1 => 9, 17 => 11, 4096 => 23, _ => 5 }
}
