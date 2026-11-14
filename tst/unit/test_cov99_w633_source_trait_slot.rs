trait Relation<A> {
    type Item;
}

impl Relation<u8> for u16 {
    type Item = ();
}

impl Relation<u8> for u32 {
    type Item = ();
}

struct Probe;

trait Select<A> {
    fn select<B, F>(&self, a: A, f: F)
    where
        B: Relation<A, Item = ()>,
        F: FnOnce() -> B;
}

impl<A> Select<A> for Probe {
    fn select<B, F>(&self, _: A, _: F)
    where
        B: Relation<A, Item = ()>,
        F: FnOnce() -> B,
    {
    }
}

fn main() {
    Probe.select(0u8, || 0u16);
}
