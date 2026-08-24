//@ test-harness

trait Layout {
    type Uninit: ?Sized;
}

#[repr(transparent)]
struct Uninit<T: ?Sized + Layout>(T::Uninit);

impl<T: ?Sized + Layout> Uninit<T> {
    fn pass(self)
    where
        T: Sized,
        Self: Sized,
    {
        take(self);
    }
}

fn take<T>(_: T) {}

struct Byte;

impl Layout for Byte {
    type Uninit = u8;
}

#[test]
fn sized_transparent_associated_tail() {
    Uninit::<Byte>(7).pass();
}
