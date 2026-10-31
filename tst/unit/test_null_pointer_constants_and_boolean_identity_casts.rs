const NULL: *const u8 = std::ptr::null();
fn main() {
    let first = NULL;
    let second = first;
    let yes = true as bool;
    let no = false as bool;
    assert!(first.is_null() && second.is_null());
    assert_eq!((!yes, !no, yes as u8, no as i16), (false, true, 1, 0));
}
