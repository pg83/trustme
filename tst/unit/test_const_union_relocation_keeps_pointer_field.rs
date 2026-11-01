union Word { number:usize, pointer:*const u16 }
static DATA:u16 = 257;
const WORD: Word = Word { pointer:&DATA };
fn main() { let word=std::hint::black_box(WORD);unsafe { assert_eq!(*word.pointer,257);assert_eq!(word.pointer,&DATA as *const u16); } }
