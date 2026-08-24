trait Trait<T, U> {}

impl<T, U> Trait<T, U> for () {}

fn main() {
    let value = &() as &'static dyn for<'a> Trait<&'static (), &'a ()>;
    let _ = value as &'static dyn Trait<&'static (), &'static ()>;
}
