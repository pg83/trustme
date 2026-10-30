//@ aux-build: serialized_legacy_asm.rs
fn main() {
    unsafe {
        assert_eq!(serialized_legacy_asm::preserve(19u32), 19);
        assert_eq!(serialized_legacy_asm::preserve((23u16, 29u16)), (23, 29));
    }
}
