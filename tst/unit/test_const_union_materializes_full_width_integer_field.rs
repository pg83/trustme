union Word { value:i32, byte:u8 }
const WORD: Word = Word { value:-17 };
fn main() { let word=std::hint::black_box(WORD);unsafe { assert_eq!(word.value,-17); } }
