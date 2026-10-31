fn make() -> impl Fn() -> (u16, u32) {
    || (257, 65537)
}

fn main() {
    let call = make();
    assert_eq!(call(), (257u16, 65537u32));
    assert_eq!(call(), call());
}
