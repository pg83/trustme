//@ compile-flags: -Znext-solver=globally
trait Factory { type Items: Iterator<Item: Copy + Into<u64>>; fn items() -> Self::Items; }
struct Input;
impl Factory for Input { type Items = std::array::IntoIter<u16,2>; fn items() -> Self::Items { [13,17].into_iter() } }
trait Middle { type Source: Factory<Items: Clone>; }
struct Route;
impl Middle for Route { type Source = Input; }
fn one<F: Factory<Items: Clone>>() -> u64 { let values=F::items(); let copy=values.clone(); values.chain(copy).map(Into::into).sum() }
fn two<M: Middle>() -> u64 { one::<M::Source>() }
fn main() { assert_eq!(two::<Route>(), 60); }
