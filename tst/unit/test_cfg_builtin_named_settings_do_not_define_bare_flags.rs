fn main() {
    assert!(!cfg!(target_os));
    assert!(!cfg!(target_arch));
    assert!(!cfg!(target_pointer_width));
    assert!(cfg!(any(target_pointer_width = "16", target_pointer_width = "32", target_pointer_width = "64")));
}
