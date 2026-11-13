#[derive(Clone, Copy)]
#[repr(align(32))]
struct Marker;

#[inline(never)]
fn array_element(values: &[Marker; 3], index: usize) -> &Marker {
    &values[index]
}

#[inline(never)]
fn slice_element(values: &[Marker], index: usize) -> &Marker {
    &values[index]
}

fn main() {
    let values = [Marker; 3];
    assert_eq!(core::mem::size_of::<Marker>(), 0);
    assert_eq!(core::mem::align_of::<Marker>(), 32);
    for index in 0..values.len() {
        let from_array = array_element(&values, index) as *const Marker;
        let from_slice = slice_element(&values, index) as *const Marker;
        assert!(!from_array.is_null());
        assert!(!from_slice.is_null());
        assert_eq!(from_array as usize % 32, 0);
        assert_eq!(from_slice as usize % 32, 0);
    }
}
