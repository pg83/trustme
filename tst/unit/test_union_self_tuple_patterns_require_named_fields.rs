//@ compile-fail: Couldn't find constant name
union Bits { word: u32, bytes: [u8; 4] }
impl Bits {
    fn inspect(self) -> u32 {
        let Self(word) = self;
        word
    }
}
fn main() { let _ = Bits { word: 7 }.inspect(); }
