use u16 as Word;
fn main() { let value: Word = 257; assert_eq!(value.to_le_bytes(), [1, 1]); }
