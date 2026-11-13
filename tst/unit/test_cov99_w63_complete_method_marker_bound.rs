//@ compile-fail: error

trait Marker {}

trait Probe {
    fn probe<T: Marker>(&self, _: T) {}
}

struct Receiver;
impl Probe for Receiver {}

fn main() {
    Receiver.probe::<bool>(false);
}
