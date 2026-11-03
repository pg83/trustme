fn main() {
    let pointer = core::ptr::null();
    let empty = pointer.is_null();
    let bytes = pointer.cast::<u8>();
    let typed: *const u32 = pointer;
    assert!(empty);
    assert!(bytes.is_null());
    assert!(typed.is_null());
    let value = 353u32;
    let mut later: *const _ = core::ptr::null();
    later = &value;
    assert_eq!(unsafe { *later }, 353);
}
