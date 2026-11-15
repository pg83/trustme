//@ edition: 2021
#![allow(dependency_on_unit_never_type_fallback)]

trait Value {
    fn get(&self) -> u8;
}

impl Value for () {
    fn get(&self) -> u8 {
        0
    }
}

struct Payload<T>(T);

fn boxed<T>(value: Payload<T>) -> Box<T> {
    Box::new(value.0)
}

struct Sink;

impl Sink {
    fn take(&self, value: Box<dyn Value>) -> u8 {
        value.get()
    }
}

fn early_return() -> u8 {
    Sink.take(boxed(Payload(return 7)))
}

fn main() {
    assert_eq!(early_return(), 7);
}
