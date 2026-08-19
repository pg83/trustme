// Two mentions of the same supertrait can pin the same associated type twice:
// once through a projection that comes out as a concrete type, once written
// out. The written-out one is what the trait object carries.
trait ConstI32 {
    type Out;
}

impl<T: ?Sized> ConstI32 for T {
    type Out = i32;
}

trait Base {
    type Output;
}

trait NormalizingHelper: Base<Output = <Self as ConstI32>::Out> + Base<Output = i32> {
    type Target;

    fn get(&self) -> i32;
}

impl Base for u32 {
    type Output = i32;
}

impl NormalizingHelper for u32 {
    type Target = i32;

    fn get(&self) -> i32 {
        7
    }
}

fn main() {
    let x: Box<dyn NormalizingHelper<Target = i32>> = Box::new(2u32);
    assert_eq!(x.get(), 7);
    let y: Box<dyn NormalizingHelper<Target = i32, Output = i32>> = Box::new(3u32);
    assert_eq!(y.get(), 7);
}
