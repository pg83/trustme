#![feature(const_closures, const_trait_impl)]
macro_rules! evaluate_twice {
    ($expression:expr) => {{
        const LEFT: u8 = ($expression)();
        const RIGHT: u8 = ($expression)();
        (LEFT, RIGHT)
    }};
}
fn main() {
    assert_eq!(evaluate_twice!((const || 11u8)), (11, 11));
    let panic = std::panic::catch_unwind(|| {
        assert!(const { (const || false)() });
    }).unwrap_err();
    let text = panic.downcast_ref::<String>().map(String::as_str)
        .or_else(|| panic.downcast_ref::<&str>().copied()).unwrap();
    assert!(text.contains("const |"));
}
