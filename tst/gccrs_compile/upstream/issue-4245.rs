pub unsafe fn copy_one<T>(source: *const T, destination: *mut T) {
    unsafe { std::ptr::copy_nonoverlapping(source, destination, 1) }
}
