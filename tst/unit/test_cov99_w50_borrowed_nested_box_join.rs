fn choose(outer: bool, inner: bool) -> Option<u32> {
    let selected = if outer {
        &(if inner {
            Box::new(None)
        } else {
            Box::new(Some(23u32))
        })
    } else {
        &Box::new(Some(41u32))
    };
    **selected
}

fn main() {
    assert_eq!(choose(true, true), None);
    assert_eq!(choose(true, false), Some(23));
    assert_eq!(choose(false, true), Some(41));
    assert_eq!(choose(false, false), Some(41));
}
