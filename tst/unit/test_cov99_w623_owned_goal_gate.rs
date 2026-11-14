//@ compile-fail: multiple applicable items in scope
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(auto_traits, negative_impls)]

auto trait Gate {}
impl<T: ?Sized> !Gate for &T {}
impl<T: ?Sized> !Gate for &mut T {}

struct Item;

trait First {
    fn owned_goal(self) -> Self;
}

impl<T: Gate> First for T {
    fn owned_goal(self) -> Self { self }
}

trait Second {
    fn owned_goal(self) -> Self;
}

impl<T: Gate> Second for T {
    fn owned_goal(self) -> Self { self }
}

fn main() {
    let _: &Item = Item.owned_goal();
}
