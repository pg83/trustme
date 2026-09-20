// A trait may list the same supertrait twice with different arguments, and
// each occurrence owns its own block of vtable slots. Naming a slot therefore
// takes the whole trait reference, arguments included: matching on the trait's
// path alone sends both calls below to whichever block is found first, and the
// one that loses gets a `&Bar` where its slot expects a `&Foo`.

trait Compare: PartialEq<Foo> + PartialEq<Bar> {}

struct Foo;
struct Bar;
struct Both;

impl PartialEq<Foo> for Both {
    fn eq(&self, _rhs: &Foo) -> bool {
        true
    }
}

impl PartialEq<Bar> for Both {
    fn eq(&self, _rhs: &Bar) -> bool {
        false
    }
}

impl Compare for Both {}

fn main() {
    let value = &Both as &dyn Compare;
    assert!(*value == Foo);
    assert!(!(*value != Foo));
    assert!(*value != Bar);
    assert!(!(*value == Bar));
}
