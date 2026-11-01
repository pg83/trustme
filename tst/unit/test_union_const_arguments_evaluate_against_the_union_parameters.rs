union Packet<T: Copy, const N: usize> { value: T, bytes: [u8; N] }
const fn width() -> usize { 4 }
type Word = Packet<u32, { width() }>;
const WORD: Packet<u32, { width() }> = Packet { value: 0x1234_5678 };
fn main() {
    assert_eq!(core::mem::size_of::<Word>(), 4);
    assert_eq!(unsafe { WORD.value }, 0x1234_5678);
    let bytes = Packet::<u32, { width() }> { bytes: [3, 5, 7, 11] };
    assert_eq!(unsafe { bytes.bytes }, [3, 5, 7, 11]);
}
