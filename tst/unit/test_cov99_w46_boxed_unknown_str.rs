//@ compile-fail: str

fn boxed<T>() -> Box<T> {
    loop {}
}

fn main() {
    let _: Box<str> = boxed();
}
