trait Leaf<Witness> {}
impl<Witness> Leaf<Witness> for u8 {}

trait Root<T> {}
impl<T> Root<T> for ()
where
    T: Leaf<()> + Leaf<bool> + Leaf<char> + Leaf<u8>
        + Leaf<u16> + Leaf<u32> + Leaf<u64> + Leaf<i8>,
{}

struct Get;
impl Get {
    fn value(self) -> u8 { 7 }
}

fn make<T>() -> T { loop {} }

fn seed<T, F>(_: T, _: F)
where
    (): Root<T>,
    F: FnOnce() -> T,
{}

fn main() {
    if false {
        seed(make(), || Get.value());
    }
}
