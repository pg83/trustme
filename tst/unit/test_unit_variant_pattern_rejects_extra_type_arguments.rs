//@ compile-fail: Incorrect parameter count

enum Token<T> {
    Empty,
    Value(T),
}

fn main() {
    match Token::<u8>::Empty {
        Token::<u8, u16>::Empty => {}
        _ => {}
    }
}
