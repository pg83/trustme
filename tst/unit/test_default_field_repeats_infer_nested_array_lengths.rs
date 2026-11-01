#![feature(default_field_values)]

struct Shape<T> {
    marker: Option<T> = None,
    widths: [[usize; 2]; 3] = [[core::mem::size_of::<T>(); _]; _],
}

fn main() {
    let small: Shape<u8> = Shape { .. };
    let large: Shape<u32> = Shape { .. };
    assert!(small.marker.is_none());
    assert!(large.marker.is_none());
    assert_eq!(small.widths, [[1, 1]; 3]);
    assert_eq!(large.widths, [[4, 4]; 3]);
}
