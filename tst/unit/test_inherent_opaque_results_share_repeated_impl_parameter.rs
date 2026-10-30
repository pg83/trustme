trait Twin { fn values(self)->Vec<u8>; }
impl<F:Iterator<Item=u8>> Twin for (F,F) { fn values(self)->Vec<u8> { self.0.chain(self.1).collect() } }
struct Ops<const N:usize>;
impl<const N:usize> Ops<N> {
 fn sequence<T:Copy>(value:T)->impl Iterator<Item=T> { std::iter::repeat(value).take(N) }
}
fn main() { assert_eq!((Ops::<2>::sequence(3u8),Ops::<2>::sequence(5u8)).values(),vec![3,3,5,5]); }
