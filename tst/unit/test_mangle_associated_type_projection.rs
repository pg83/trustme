trait Front {
    type Back;
}

impl<T> Front for Vec<T> {
    type Back = Vec<T>;
}

struct Holder<T: Front>(Vec<T::Back>);
struct Recursive(Holder<Vec<Recursive>>);

fn main() {
    assert!(core::mem::size_of::<Recursive>() > 0);
}
