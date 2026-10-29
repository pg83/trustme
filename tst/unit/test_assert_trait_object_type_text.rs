fn main() {
    let panic = std::panic::catch_unwind(|| {
        assert!({
            let callback: &(dyn for<'a> Fn(&'a u8) -> u8 + Send + Sync) = &|value| *value;
            callback(&7) == 8
        });
    }).unwrap_err();
    let text = panic.downcast_ref::<String>().map(String::as_str)
        .or_else(|| panic.downcast_ref::<&str>().copied()).unwrap();
    assert!(text.contains("callback"));
    assert!(text.contains("Send"));
    assert!(text.contains("Sync"));
}
