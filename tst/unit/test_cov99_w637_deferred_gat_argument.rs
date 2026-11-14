//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Pick {
    type Output;
}

trait G {
    type Item<V>;
}

trait Visit {
    fn visit(&self);
}

struct Carrier<T>(Option<T>);

impl<T, U> Visit for Carrier<T>
where
    T: Pick<Output = U> + G<Item<U> = ()>,
{
    fn visit(&self) {}
}

impl Pick for u8 { type Output = u32; }
impl Pick for u16 { type Output = u64; }
impl G for u8 { type Item<V> = (); }
impl G for u16 { type Item<V> = (); }

pub fn exercise() {
    let mut value = Carrier(None);
    value.visit();
    value.0 = Some(7u8);
}
