trait Family { type Item; }
impl Family for u8 { type Item=[u16;3]; }
trait Build { const LENGTH:usize; }
impl<T:Family,U,const N:usize> Build for (T,T::Item,[U;N]) { const LENGTH:usize=N; }
fn length<A>()->usize where (u8,A,A):Build { <(u8,A,A) as Build>::LENGTH }
fn main() { assert_eq!(length::<_>(),3); }
