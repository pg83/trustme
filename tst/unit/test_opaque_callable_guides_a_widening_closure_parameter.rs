fn make() -> impl Fn(u8) -> u16 {
    |value| u16::from(value) + 257
}

fn main() {
    let call = make();
    assert_eq!(call(0), 257);
    assert_eq!(call(255), 512);
}
