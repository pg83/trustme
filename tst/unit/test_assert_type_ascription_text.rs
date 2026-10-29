#![feature(type_ascription, builtin_syntax)]
fn main() {
    let panic = std::panic::catch_unwind(|| assert!(builtin # type_ascribe(3, u16) == 4)).unwrap_err();
    let text = panic.downcast_ref::<String>().map(String::as_str)
        .or_else(|| panic.downcast_ref::<&str>().copied()).unwrap();
    let compact: String = text.chars().filter(|c| !c.is_whitespace()).collect();
    assert!(compact.contains("type_ascribe(3,u16)"));
}
