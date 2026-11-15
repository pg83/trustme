//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Produce {
    type Value;
    fn produce(&self) -> Self::Value;
}

struct Supply<F>(F);

impl<F, V> Produce for Supply<F>
where
    F: Fn() -> V,
{
    type Value = V;
    fn produce(&self) -> V { (self.0)() }
}

fn main() {
    let supply = Supply(|| None);
    let object: &dyn Produce<Value = Option<_>> = &supply;
    let value = object.produce();
    let _: Option<u8> = value;
}
