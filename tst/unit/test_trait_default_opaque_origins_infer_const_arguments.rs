trait Factory<T: Copy, const N: usize> {
 fn sequence(value: T) -> impl Iterator<Item = T> + Clone { [value; N].into_iter() }
}
struct Source;
impl<T: Copy, const N: usize> Factory<T, N> for Source {}
fn join<T: Iterator<Item=u8>>(first:T,second:T)->Vec<u8>{first.chain(second).collect()}
fn main(){assert_eq!(join(<Source as Factory<u8,2>>::sequence(23),Source::sequence(29)),vec![23,23,29,29]);}
