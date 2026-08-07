use std::mem::{align_of_val, size_of_val, ManuallyDrop};

#[repr(C, packed(2))]
struct Packed<T: ?Sized>(u8, ManuallyDrop<T>);

fn main() {
    let packed = Packed(0, ManuallyDrop::new(1usize));
    let sized: &Packed<usize> = &packed;
    let wide: &Packed<dyn Send> = sized;

    assert_eq!(size_of_val(sized), size_of_val(wide));
    assert_eq!(align_of_val(sized), align_of_val(wide));
}
