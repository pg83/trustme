// Extracted from library/std/src/keyword_docs.rs:2312
#![allow(unused)]
fn main() {
    fn select<'short, 'long>(s1: &'short str, s2: &'long str, second: bool) -> &'short str
    where
        'long: 'short,
    {
        if second { s2 } else { s1 }
    }

    let outer = String::from("Long living ref");
    let longer = &outer;
    {
        let inner = String::from("Short living ref");
        let shorter = &inner;

        assert_eq!(select(shorter, longer, false), shorter);
        assert_eq!(select(shorter, longer, true), longer);
    }
}
