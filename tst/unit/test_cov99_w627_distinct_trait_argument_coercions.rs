//@ compile-fail: multiple applicable items in scope
//@ compile-flags: --emit=metadata

struct Probe;

trait Narrow<T> {
    fn route(&self, value: T);
}

trait Wide<T> {
    fn route(&self, value: T);
}

impl Narrow<Option<u8>> for Probe {
    fn route(&self, _: Option<u8>) {}
}

impl Wide<Option<u16>> for Probe {
    fn route(&self, _: Option<u16>) {}
}

fn main() {
    Probe.route(None);
}
