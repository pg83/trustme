fn promoted() -> &'static dyn Fn(u16) -> u16 {
    &|value| {
        let offset = 3u16;
        let adjusted = value + offset;
        adjusted * 2
    }
}

fn main() {
    let operation = promoted();
    assert_eq!(operation(5), 16);
    assert_eq!(promoted()(11), 28);
}
