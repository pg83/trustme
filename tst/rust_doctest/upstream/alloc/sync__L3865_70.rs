// Extracted from library/alloc/src/sync.rs:3865
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    use std::borrow::Cow;
    let cow: Cow<'_, str> = Cow::Borrowed("eggplant");
    let shared: Arc<str> = Arc::from(cow);
    assert_eq!("eggplant", &shared[..]);
}
