fn main() {
    let value = 367u32;
    let pointer: *const _ = &value;
    let mut slot: Option<*const _> = Some(pointer);
    let selected = slot.take().unwrap();
    assert!(!selected.is_null());
    assert_eq!(unsafe { *selected }, 367);
    assert_eq!(selected.cast::<u8>(), (&value as *const u32).cast::<u8>());
    let empty: *const u32 = core::ptr::null();
    assert!(empty.is_null());
    assert!(slot.is_none());
}
