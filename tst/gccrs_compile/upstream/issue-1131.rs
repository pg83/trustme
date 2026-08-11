pub fn size_and_offset<T>(pointer: *const T, count: isize) -> (usize, *const T) {
    (std::mem::size_of::<T>(), pointer.wrapping_offset(count))
}
