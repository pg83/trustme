//@ run-pass

trait Trait {
    fn type_name(&self) -> &'static str;
}

impl<T: ?Sized> Trait for T {
    fn type_name(&self) -> &'static str {
        core::any::type_name::<T>()
    }
}

fn method<T: ?Sized>() -> fn(&T) -> &'static str {
    const { Trait::type_name as fn(&T) -> &'static str }
}

fn main() {
    assert_eq!(method::<dyn Trait>()(&0_i32), "i32");
}
