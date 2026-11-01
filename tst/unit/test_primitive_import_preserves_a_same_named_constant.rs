#![allow(non_upper_case_globals)]
const u16: u16 = 73;
use u16 as Word;
fn main() { let value: Word = Word; assert_eq!(value, 73); }
