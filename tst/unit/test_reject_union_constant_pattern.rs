//@ compile-fail: Union constants
union Word { number: u32, bytes: [u8; 4] }
const KEY: Word = Word { number: 37 };
fn main() {
    let word = Word { number: 37 };
    let _ = match word { KEY => true, _ => false };
}
