union Word { number: u32, bytes: [u8; 4] }
fn main() {
    let mut output = 0;
    unsafe { Word { number: output } = Word { number: 0x11223344 }; }
    assert_eq!(output, 0x11223344);
}
