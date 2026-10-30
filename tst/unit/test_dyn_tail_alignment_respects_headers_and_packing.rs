use std::mem::{align_of, align_of_val, size_of, size_of_val, ManuallyDrop};

#[repr(C, packed(2))]
struct Tight<T: ?Sized>(u8, ManuallyDrop<T>);
#[repr(C)]
struct Header<T: ?Sized>(u16, T);
#[repr(align(32))]
struct Aligned(u8);

fn check_tight<T: Send>(value: T) {
    let object = Tight(7, ManuallyDrop::new(value));
    let wide: &Tight<dyn Send> = &object;
    assert_eq!(align_of_val(wide), align_of::<Tight<T>>());
    assert_eq!(size_of_val(wide), size_of::<Tight<T>>());
}

fn check_header<T: Send>(value: T) {
    let object = Header(11, value);
    let wide: &Header<dyn Send> = &object;
    assert_eq!(align_of_val(wide), align_of::<Header<T>>());
    assert_eq!(size_of_val(wide), size_of::<Header<T>>());
}

fn main() {
    check_tight(9u8);
    check_tight(17u64);
    check_tight(Aligned(3));
    check_header(5u8);
    check_header(13u64);
    check_header(Aligned(7));
    let bytes: &[u32] = &[2, 3, 5];
    assert_eq!(align_of_val(bytes), align_of::<u32>());
    assert_eq!(align_of_val("abc"), 1);
}
