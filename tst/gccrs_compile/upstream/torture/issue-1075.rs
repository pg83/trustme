pub fn slice_parts<T>(slice: *const [T]) -> (*const T, usize) {
    (slice as *const T, slice.len())
}

pub const unsafe fn advance<T>(pointer: *const T, count: usize) -> *const T {
    unsafe { pointer.add(count) }
}
