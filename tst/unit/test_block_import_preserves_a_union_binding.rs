fn main() {
    union Bits { word: u32, bytes: [u8; 4] }
    use Bits as Raw;
    let value = Raw { word: 0x12345678 };
    assert_eq!(unsafe { value.word }, 0x12345678);
}
