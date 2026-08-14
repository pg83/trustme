const EMPTY_SLICE: &[i32] = unsafe {
    std::slice::from_raw_parts(std::ptr::without_provenance(123456), 0)
};

const DANGLING_U16_SLICE: &[u16] = unsafe {
    std::slice::from_raw_parts(std::ptr::NonNull::<u16>::dangling().as_ptr(), 0)
};

fn main() {
    assert_eq!(EMPTY_SLICE.as_ptr().addr(), 123456);
    assert_eq!(EMPTY_SLICE.len(), 0);
    assert_eq!(DANGLING_U16_SLICE.as_ptr().addr(), align_of::<u16>());
    assert_eq!(DANGLING_U16_SLICE.len(), 0);
}
