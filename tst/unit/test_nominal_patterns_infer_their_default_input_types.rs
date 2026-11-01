#[derive(Default)]
struct Flag;
struct Pair(u16, u16);
impl Default for Pair { fn default() -> Self { Pair(11, 17) } }
struct Record { first: u16, second: u16 }
impl Default for Record { fn default() -> Self { Self { first: 19, second: 23 } } }
fn fresh<T: Default>() -> T { T::default() }
fn main() {
    let Flag = fresh();
    let Pair(first, second) = fresh();
    let Record { first: third, second: fourth } = fresh();
    assert_eq!((first, second, third, fourth), (11, 17, 19, 23));
}
