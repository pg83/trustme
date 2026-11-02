//@ compile-variant: -O
struct Operations;
impl Operations {
    fn first(value: u16) -> u16 { value + 3 }
    fn second(value: u16) -> u16 { value * 2 }
}
trait Third { fn value(input: u16) -> u16; }
impl Third for Operations { fn value(input: u16) -> u16 { input + 7 } }

fn repeat<T: Fn(u16) -> u16 + Copy>(first: T, second: T) -> u16 {
    first(11) + second(13)
}

fn main() {
    let functions: [fn(u16) -> u16; 4] = [
        Operations::first,
        Operations::second,
        <Operations as Third>::value,
        |value| value + 17,
    ];
    assert_eq!(functions[0](19), 22);
    assert_eq!(functions[1](19), 38);
    assert_eq!(functions[2](19), 26);
    assert_eq!(functions[3](19), 36);
    assert_eq!(repeat(Operations::first, Operations::first), 30);
    let closure = |value| value + 5;
    assert_eq!(repeat(closure, closure), 34);
}
