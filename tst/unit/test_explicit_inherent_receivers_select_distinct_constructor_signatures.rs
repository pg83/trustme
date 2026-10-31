struct Choice<T>(T);
impl Choice<u8> { fn choose(value: u8) -> Self { Choice(value + 1) } }
impl Choice<u16> { fn choose(value: u16) -> Self { Choice(value + 5) } }
fn main() {
    assert_eq!(Choice::<u8>::choose(12).0, 13);
    assert_eq!(Choice::<u16>::choose(32).0, 37);
    let choose: fn(u8) -> Choice<u8> = Choice::<u8>::choose;
    assert_eq!(choose(20).0, 21);
}
