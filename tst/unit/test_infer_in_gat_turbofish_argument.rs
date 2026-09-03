// A `_` written inside an explicit generic argument needs an inference
// variable allocated for it like any other placeholder, including when it
// sits in the arguments of a generic associated type inside that argument.

trait TraitA {}

trait TraitB {
    type Assoc<T: ?Sized>;
}

impl<T: TraitB> TraitA for (T, T::Assoc<bool>) {}

impl TraitB for i32 {
    type Assoc<T: ?Sized> = u32;
}

fn needs_a<T: TraitA>() -> u8 {
    3
}

fn bar<T: TraitB>() -> u8 {
    needs_a::<(T, <T as TraitB>::Assoc<_>)>()
}

fn main() {
    assert_eq!(bar::<i32>(), 3);
}
