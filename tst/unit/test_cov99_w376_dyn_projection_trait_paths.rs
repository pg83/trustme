//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

#![allow(dead_code)]

trait Left { type Item; }
trait Right { type Item; }
trait Object<T> { type Item; }
trait Pick<K> {}

impl<T: Left> Pick<()> for dyn Object<T, Item = <T as Left>::Item> {}
impl<T: Right> Pick<u8> for dyn Object<T, Item = <T as Right>::Item> {}

fn require_pick<T: Pick<u8> + ?Sized>(_: &T) {}

fn choose<T: Left + Right>(object: &(dyn Object<T, Item = <T as Right>::Item> + 'static)) {
    require_pick(object);
}
