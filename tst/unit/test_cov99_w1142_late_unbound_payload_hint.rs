//@ edition: 2021

trait Value {
    fn get(&self) -> u8;
}

impl Value for u8 {
    fn get(&self) -> u8 {
        *self
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

fn main() {
    assert_eq!(Sink.take(boxed(Payload(7_u8))), 7);
}
