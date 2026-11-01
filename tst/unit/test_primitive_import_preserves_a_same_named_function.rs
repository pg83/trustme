fn u16() -> u16 { 83 }
use u16 as Word;
fn main() { let value: Word = Word(); assert_eq!(value, 83); }
