trait Stamp<const N: usize> {}

impl Stamp<1> for () {}
impl Stamp<2> for () {}

fn first<T: Stamp<1>>(value: T) -> T { value }
fn second<T: Stamp<2>>(value: T) -> T { value }

fn main() {
    first(());
    second(());
}
