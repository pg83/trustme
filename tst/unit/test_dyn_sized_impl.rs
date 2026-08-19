// An inherent impl on a trait object that asks for `Self: Sized` can never be
// used, and neither can a trait method kept out of the vtable the same way, but
// both still have to compile.
pub trait TestTrait {
    type MyType;
    fn func() -> Option<Self>
    where
        Self: Sized;
}

impl<T> dyn TestTrait<MyType = T>
where
    Self: Sized,
{
    pub fn other_func() -> Option<Self> {
        match Self::func() {
            Some(me) => Some(me),
            None => None,
        }
    }
}

trait Foo {
    fn foo() -> impl Sized
    where
        Self: Sized,
    {
    }
}

impl Foo for () {}

impl TestTrait for u8 {
    type MyType = u8;
    fn func() -> Option<Self> {
        Some(7)
    }
}

fn main() {
    let _x: &dyn Foo = &();
    assert_eq!(<u8 as TestTrait>::func(), Some(7));
}
