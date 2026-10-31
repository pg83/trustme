fn main() {
    let pointer = std::ptr::null_mut() as *const [u32; 3];
    assert!(pointer.is_null());
}
