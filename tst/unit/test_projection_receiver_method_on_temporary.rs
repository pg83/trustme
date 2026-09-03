// A method call whose receiver is an associated-type projection finds the
// method through the projection's declared bounds. That has to hold whether
// the receiver is a place or a temporary the call must borrow for itself,
// and whether or not the call's result is used for anything.

trait Foo: Sized {
    type Baz: Default + AsMut<[u8]>;

    fn discard_on_temporary() {
        Self::Baz::default().as_mut();
    }

    fn first_of_temporary() -> u8 {
        Self::Baz::default().as_mut()[0]
    }

    fn first_of_binding() -> u8 {
        let mut value = Self::Baz::default();
        value.as_mut()[0]
    }
}

impl Foo for () {
    type Baz = [u8; 1];
}

fn main() {
    <() as Foo>::discard_on_temporary();
    assert_eq!(<() as Foo>::first_of_temporary(), 0);
    assert_eq!(<() as Foo>::first_of_binding(), 0);
}
