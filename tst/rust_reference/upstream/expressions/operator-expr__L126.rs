// Extracted from src/expressions/operator-expr.md:126
#![allow(unused)]
fn main() {
    #[repr(packed)]
    struct Packed {
        f1: u8,
        f2: u16,
    }
    
    let packed = Packed { f1: 1, f2: 2 };
    // `&packed.f2` would create an unaligned reference, and thus be undefined behavior!
    let raw_f2 = &raw const packed.f2;
    assert_eq!(unsafe { raw_f2.read_unaligned() }, 2);
}
