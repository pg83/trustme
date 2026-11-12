#![feature(negative_impls)]

struct Envelope(u16);

unsafe trait Probe {
    fn value(&self) -> u16;
}

unsafe impl Probe for Envelope {
    fn value(&self) -> u16 {
        self.0 + 4
    }
}

#[cfg(any())]
unsafe impl !Send for Envelope {}

fn require_send<T: Send>(_: &T) {}

fn main() {
    let value = Envelope(983);
    require_send(&value);
    assert_eq!(value.value(), 987);
}
