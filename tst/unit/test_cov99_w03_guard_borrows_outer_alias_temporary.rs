fn select(value: Option<u32>) -> u32 {
    match value {
        Some(item)
            if {
                let borrowed = &item;
                *borrowed == 23
            } =>
        {
            item + 1
        }
        _ => 0,
    }
}

fn main() {
    assert_eq!(select(Some(23)), 24);
    assert_eq!(select(Some(24)), 0);
    assert_eq!(select(None), 0);
}
