pub unsafe fn test_const(pointer: *const u8) -> *const u8 {
    unsafe { pointer.offset(1) }
}

pub unsafe fn test_mut(pointer: *mut u8) -> *mut u8 {
    unsafe { pointer.offset(1) }
}
