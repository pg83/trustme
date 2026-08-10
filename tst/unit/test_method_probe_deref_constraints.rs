//@ check-pass

use std::ops::Deref;

trait Probe {
    fn probe(&self) {}
}

impl Probe for Direct<u32> {}

struct Direct<T>(T);

impl Deref for Direct<()> {
    type Target = dyn Probe + 'static;

    fn deref(&self) -> &Self::Target {
        panic!()
    }
}

impl Probe for u32 {}

struct Through<T>(T, u32);

impl Deref for Through<u8> {
    type Target = dyn Probe + 'static;

    fn deref(&self) -> &Self::Target {
        &self.1
    }
}

fn main() {
    let direct = Direct(0);
    direct.probe();

    let through = Through(Default::default(), 0);
    through.probe();
}
