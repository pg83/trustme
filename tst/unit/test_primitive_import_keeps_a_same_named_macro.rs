macro_rules! u16 { () => { 107u16 }; }
use u16 as Word;
fn main() { let value: Word = Word!(); assert_eq!(value, 107); }
