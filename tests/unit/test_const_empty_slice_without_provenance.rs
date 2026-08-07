const EMPTY_SLICE: &[i32] = unsafe {
    std::slice::from_raw_parts(std::ptr::without_provenance(123456), 0)
};

fn main() {
    assert_eq!(EMPTY_SLICE.as_ptr().addr(), 123456);
    assert_eq!(EMPTY_SLICE.len(), 0);
}
