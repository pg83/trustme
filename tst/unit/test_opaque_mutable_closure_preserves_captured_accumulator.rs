fn make(start: u32) -> impl FnMut(u16) -> u32 {
    let mut total = start;
    move |value| {
        total += u32::from(value);
        total
    }
}

fn main() {
    let mut call = make(65536);
    assert_eq!(call(3), 65539);
    assert_eq!(call(5), 65544);
}
