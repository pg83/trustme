// Extracted from library/core/src/option.rs:345
#![allow(unused)]
fn main() {
    use std::collections::BTreeMap;
    let mut bt = BTreeMap::new();
    bt.insert(20u8, "foo");
    bt.insert(42u8, "bar");
    let res = [0u8, 1, 11, 200, 22]
        .into_iter()
        .map(|x| {
            // `checked_sub()` returns `None` on error
            x.checked_sub(1)
                // same with `checked_mul()`
                .and_then(|x| x.checked_mul(2))
                // `BTreeMap::get` returns `None` on error
                .and_then(|x| bt.get(&x))
                // Substitute an error message if we have `None` so far
                .or(Some(&"error!"))
                .copied()
                // Won't panic because we unconditionally used `Some` above
                .unwrap()
        })
        .collect::<Vec<_>>();
    assert_eq!(res, ["error!", "error!", "foo", "error!", "bar"]);
}
