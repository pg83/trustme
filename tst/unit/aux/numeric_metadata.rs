pub const SIGNED: i128 = -(1_i128 << 100) + 17;
pub const UNSIGNED: u128 = (1_u128 << 110) + 29;
pub const FLOAT: f64 = -3.75;
pub fn adjust(value: i128) -> i128 { value - 19 }
