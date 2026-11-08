fn make<const STEP: u16>(base: u16) -> impl Fn(u16) -> u16 {
    move |amount| base + amount + STEP
}

fn main() {
    let first = make::<7>(19);
    let second = make::<13>(29);
    assert_eq!(first(23), 49);
    assert_eq!(second(31), 73);
    assert_eq!(first(5), 31);
}
