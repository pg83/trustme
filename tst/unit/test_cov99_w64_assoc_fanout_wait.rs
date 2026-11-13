trait Leaf<Witness> {}
impl<Witness> Leaf<Witness> for u8 {}

trait Root<T> {}
impl<T> Root<T> for ()
where
    T: Leaf<()> + Leaf<bool> + Leaf<char> + Leaf<u8>
        + Leaf<u16> + Leaf<u32> + Leaf<u64> + Leaf<i8>,
{}

fn seed<T>(_: T)
where
    (): Root<T>,
{}

fn main() {
    seed(7u8);
}
