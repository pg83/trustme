#![feature(default_field_values)]

struct Defaults<T, const N: usize> {
    marker: Option<T> = None,
    widths: [usize; N] = [core::mem::size_of::<T>(); N],
}

fn main() {
    let small: Defaults<u16, 3> = Defaults { .. };
    let large: Defaults<u64, 2> = Defaults { .. };
    assert!(small.marker.is_none());
    assert!(large.marker.is_none());
    assert_eq!(small.widths, [2, 2, 2]);
    assert_eq!(large.widths, [8, 8]);
}
