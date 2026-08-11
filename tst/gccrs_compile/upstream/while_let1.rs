fn next_value(first: &mut bool) -> Option<i32> {
    if *first {
        *first = false;
        Some(15)
    } else {
        None
    }
}

pub fn test() {
    let mut first = true;
    while let Some(15) = next_value(&mut first) {}
}
