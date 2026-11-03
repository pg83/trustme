//@ crate-type: lib

unsafe extern "C-unwind" {
    fn _Unwind_RaiseException(exception: *mut ()) -> i32;
}

#[no_mangle]
pub unsafe extern "C-unwind" fn forward_exception(exception: *mut ()) -> i32 {
    _Unwind_RaiseException(exception)
}
