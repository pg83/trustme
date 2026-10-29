#![feature(impl_trait_in_bindings)]
fn main() {
    let failure = std::panic::catch_unwind(|| assert!({
        let values: impl Iterator<Item: Copy + std::fmt::Debug> = [2u8, 5].into_iter();
        values.count() == 3
    })).unwrap_err();
    let message = failure.downcast_ref::<String>().map(String::as_str)
        .or_else(|| failure.downcast_ref::<&str>().copied()).unwrap();
    let compact: String = message.split_whitespace().collect();
    assert!(compact.contains("Iterator<Item:Copy+std::fmt::Debug>"));
}
