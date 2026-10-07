//@ aux-build: no_core_with_core.rs

fn main() {
    assert_eq!(no_core_with_core::checked_div(6, 3), 2);
    let payload = std::panic::catch_unwind(|| no_core_with_core::checked_div(1, 0)).unwrap_err();
    assert_eq!(payload.downcast_ref::<String>().map(String::as_str), Some("division of 1 by zero"));
}
