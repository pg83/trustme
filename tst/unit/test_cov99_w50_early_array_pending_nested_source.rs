fn join(inner: bool) {
    let selected: [_; 2];
    let nested = if inner {
        Box::new(None)
    } else {
        Box::new(Some(23u32))
    };
    let fixed = Box::new(Some(41u32));
    selected = [&nested, &fixed];
    let _ = selected;
}

fn main() {
    join(true);
    join(false);
}
