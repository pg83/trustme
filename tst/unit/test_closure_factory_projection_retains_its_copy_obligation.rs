trait Project {
    type Output: Copy;
    fn project(self) -> Self::Output;
}

impl<T: Copy> Project for (T,) {
    type Output = T;
    fn project(self) -> T { self.0 }
}

trait Factory {
    type Product;
    fn build(self) -> Self::Product;
}

struct Maker<F>(F);

impl<F, T: Copy> Factory for Maker<F>
where
    F: FnOnce() -> T,
{
    type Product = <(T,) as Project>::Output;
    fn build(self) -> Self::Product {
        ((self.0)(),).project()
    }
}

fn finish<F, T>(factory: F) -> T
where
    F: Factory<Product = T>,
    T: Copy,
{
    let first = factory.build();
    let retained = first;
    let _copy = first;
    retained
}

fn main() {
    let seed = 13u16;
    let short: u16 = finish(Maker(move || seed + 16));
    let wide: u64 = finish(Maker(|| 37u64));
    assert_eq!(short, 29);
    assert_eq!(wide, 37);
}
