pub unsafe fn byte_len<T>(count: usize) -> usize {
    std::mem::size_of::<T>() * count
}

pub unsafe fn swap_nonoverlapping<T>(left: *mut T, right: *mut T, count: usize) {
    unsafe { std::ptr::swap_nonoverlapping(left, right, count) }
}
