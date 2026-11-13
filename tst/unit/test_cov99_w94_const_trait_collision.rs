trait Stamp<const N: usize> {}

impl Stamp<1> for () {}
impl Stamp<2> for () {}

struct Both<T: Stamp<1> + Stamp<2>>(T);

fn accept(_: Both<()>) {}

fn main() {
    accept(Both(()));
}
