fn main() {
    let panic = std::panic::catch_unwind(|| assert!({
        let values = [7u8; 3];
        match values.len() { 0 | 1 => true, _ => false }
    })).unwrap_err();
    let text = panic.downcast_ref::<String>().map(String::as_str)
        .or_else(|| panic.downcast_ref::<&str>().copied()).unwrap();
    let compact: String = text.chars().filter(|c| !c.is_whitespace()).collect();
    assert!(compact.contains("[7u8;3]"));
    assert!(compact.contains("0|1=>true"));
}
