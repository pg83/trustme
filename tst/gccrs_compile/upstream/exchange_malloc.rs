pub unsafe fn allocate(size: usize, align: usize) -> *mut u8 {
    let layout = unsafe { std::alloc::Layout::from_size_align_unchecked(size, align) };
    unsafe { std::alloc::alloc(layout) }
}
