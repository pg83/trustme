#![feature(return_type_notation)]

trait Factory {
    fn make() -> impl Sized;
}

fn require_send<T: Factory<make(..): Send>>() {
    fn is_send(_: impl Send) {}
    is_send(T::make());
}

fn main() {}
