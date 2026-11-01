pub use u16;
fn main() { let value: u16 = 257; assert_eq!(value.to_le_bytes(), [1, 1]); }
