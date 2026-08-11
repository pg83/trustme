pub const unsafe fn advance<T>(pointer: *const T, count: usize) -> *const T {
    unsafe { pointer.add(count) }
}
