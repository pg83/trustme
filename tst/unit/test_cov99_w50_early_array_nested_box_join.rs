fn choose(outer: bool, inner: bool) -> Option<u32> {
    let selected: [_; 2];
    let nested = if inner {
        Box::new(None)
    } else {
        Box::new(Some(23u32))
    };
    let fixed = Box::new(Some(41u32));
    selected = [&nested, &fixed];
    **selected[if outer { 0 } else { 1 }]
}

fn main() {
    assert_eq!(choose(true, true), None);
    assert_eq!(choose(true, false), Some(23));
    assert_eq!(choose(false, true), Some(41));
    assert_eq!(choose(false, false), Some(41));
}
