#![feature(generic_assert)]

fn panic_text(f: impl FnOnce() + std::panic::UnwindSafe) -> String {
    let error = std::panic::catch_unwind(f).unwrap_err();
    if let Some(text) = error.downcast_ref::<String>() { text.clone() }
    else { error.downcast_ref::<&str>().unwrap().to_string() }
}

macro_rules! compare {
    ($right:expr) => {{
        let value = 5;
        assert!(value == $right);
    }};
}

fn main() {
    let value = 9;
    let repeated = panic_text(|| assert!(value == value + 1));
    assert_eq!(repeated.matches("\n  value = ").count(), 1);
    assert!(repeated.contains("value = 9"));
    let distinct = panic_text(|| compare!(value));
    assert_eq!(distinct.matches("\n  value = ").count(), 2);
    assert!(distinct.contains("value = 5"));
    assert!(distinct.contains("value = 9"));
}
