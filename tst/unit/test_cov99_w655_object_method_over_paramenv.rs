//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort


trait Marker {}
impl Marker for i32 {}
impl Marker for i64 {}

trait Object {
    fn choose(&self, value: i32) -> u8;
}

trait First {
    fn choose<T: Marker>(&self, value: T) -> u8;
}

trait Second {
    fn choose<T: Marker>(&self, value: T) -> u8;
}

impl First for dyn Object {
    fn choose<T: Marker>(&self, _value: T) -> u8 { 1 }
}

impl Second for dyn Object {
    fn choose<T: Marker>(&self, _value: T) -> u8 { 2 }
}

fn probe(object: &(dyn Object + 'static))
where
    dyn Object: First + Second,
{
    let _: u8 = object.choose(0);
}

fn main() {}
