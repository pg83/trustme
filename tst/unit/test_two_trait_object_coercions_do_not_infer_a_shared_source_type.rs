//@ compile-fail: error:0:

trait First {}
trait Second {}
impl First for u8 {}
impl Second for u8 {}
impl First for u16 {}
impl Second for u16 {}

fn unknown<T: 'static>() -> &'static T { loop {} }

fn main() {
    let value = unknown();
    let _: &dyn First = value;
    let _: &dyn Second = value;
}
