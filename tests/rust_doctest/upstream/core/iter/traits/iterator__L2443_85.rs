// Extracted from library/core/src/iter/traits/iterator.rs:2443
#![allow(unused)]
fn main() {
    use std::fs::rename;
    use std::io::{stdout, Write};
    use std::path::Path;
    
    let data = ["no_tea.txt", "stale_bread.json", "torrential_rain.png"];
    
    let res = data.iter().try_for_each(|x| writeln!(stdout(), "{x}"));
    assert!(res.is_ok());
    
    let mut it = data.iter().cloned();
    let res = it.try_for_each(|x| rename(x, Path::new(x).with_extension("old")));
    assert!(res.is_err());
    // It short-circuited, so the remaining items are still in the iterator:
    assert_eq!(it.next(), Some("stale_bread.json"));
}
