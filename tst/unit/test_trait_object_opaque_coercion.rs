trait Trait<T> {}

impl<T, U> Trait<T> for U {}

fn widen() -> &'static (dyn Trait<impl Sized> + Send) {
    if false {
        let value = widen();
        let _: &'static dyn Trait<()> = value;
    }
    &()
}

fn narrow() -> &'static dyn Trait<impl Sized> {
    if false {
        let mut value = narrow();
        let concrete: &'static (dyn Trait<()> + Send) = &();
        value = concrete;
        let _ = value;
    }
    &()
}

fn main() {
    let _ = widen();
    let _ = narrow();
}
