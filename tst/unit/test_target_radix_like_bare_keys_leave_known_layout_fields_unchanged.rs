//@ input: support/targets/radix_like_alignment_keys.toml
//@ compile-variant: --target=$DIR/support/targets/radix_like_alignment_keys.toml
#[repr(C)]
struct Record { byte: u8, half: u16, word: u32, wide: u64 }
fn main() {
    assert_eq!(std::mem::size_of::<Record>(), 16);
    assert_eq!(std::mem::align_of::<Record>(), 8);
    assert_eq!(std::mem::offset_of!(Record, wide), 8);
}
