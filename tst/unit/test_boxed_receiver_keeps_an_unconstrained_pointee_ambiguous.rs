//@ compile-fail: error:

struct Gauge(u16);

impl Gauge {
    fn reading(self: Box<Self>) -> u16 {
        self.0
    }
}

fn seed<T>() -> T {
    panic!("seed is never executed")
}

fn main() {
    let receiver = Box::new(seed());
    let _value = receiver.reading();
}
