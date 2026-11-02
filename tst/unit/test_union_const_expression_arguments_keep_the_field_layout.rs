//@ compile-variant: -O
union Storage<const N: usize> { bytes: [u8; N], word: u64 }
fn main() {
    let value: Storage<{ 2 + 2 }> = Storage { bytes: [3, 5, 7, 11] };
    assert_eq!(unsafe { value.bytes[3] }, 11);
}
