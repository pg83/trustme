union Empty { unit: (), bytes: [u8; 0] }
const VALUE: Empty = Empty { unit: () };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe { assert_eq!(value.bytes, []); }
    assert_eq!(std::mem::size_of::<Empty>(), 0);
}
