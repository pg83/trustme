use std::mem::{align_of, align_of_val, ManuallyDrop};
#[repr(C)]
struct Header<T: ?Sized>(u16, T);
#[repr(C, packed(2))]
struct Tight<T: ?Sized>(u8, ManuallyDrop<T>);
#[repr(align(32))]
struct Aligned(u8);
const WIDE_ALIGN: usize = {
    let value = Header(3, Aligned(7));
    let wide: &Header<dyn Sync> = &value;
    align_of_val(wide)
};
const TIGHT_ALIGN: usize = {
    let value = Tight(5, ManuallyDrop::new(19u64));
    let wide: &Tight<dyn Sync> = &value;
    align_of_val(wide)
};
fn main() {
    assert_eq!(WIDE_ALIGN, align_of::<Header<Aligned>>());
    assert_eq!(TIGHT_ALIGN, align_of::<Tight<u64>>());
}
