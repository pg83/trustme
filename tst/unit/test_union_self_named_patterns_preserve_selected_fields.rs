union Bits { word: u32, bytes: [u8; 4] }
impl Bits {
    unsafe fn read(self) -> u32 {
        let Self { word } = self;
        word
    }
    unsafe fn first(self) -> u8 {
        let Self { bytes: [first, ..] } = self;
        first
    }
}
fn main() {
    assert_eq!(unsafe { Bits { word: 31 }.read() }, 31);
    assert_eq!(unsafe { Bits { bytes: [37, 41, 43, 47] }.first() }, 37);
}
