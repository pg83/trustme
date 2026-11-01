union MaybeWord { empty: (), value:u32 }
const WORD: MaybeWord = MaybeWord { value:37 };
fn main() { let word=std::hint::black_box(WORD);unsafe { assert_eq!(word.value,37); } }
