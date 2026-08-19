// Every type parameter of an impl has to be pinned down by a use: by the type
// the impl is for, by the trait arguments, or by an associated-type equality
// whose own inputs are already pinned down.
trait Equate {
    type Proj;
}
impl<T> Equate for T {
    type Proj = T;
}

trait Indirect {
    type Ty;
}

// `A` comes from the self type, `B` from the self type as well.
impl<A, B: Equate<Proj = A>> Indirect for (A, B) {
    type Ty = ();
}

trait Carrier<T> {
    fn get(&self) -> T;
}

// `U` is pinned down by the trait argument.
impl<U: Default> Carrier<U> for u8 {
    fn get(&self) -> U {
        U::default()
    }
}

// `V` is pinned down by an equality whose input is the self type.
trait Named {
    type Out;
}
impl Named for u16 {
    type Out = u32;
}
trait Sink<V> {
    fn sink(&self) -> V;
}
impl<V: Default> Sink<V> for u16
where
    u16: Named<Out = V>,
{
    fn sink(&self) -> V {
        V::default()
    }
}

// A lifetime the impl declares is pinned down by the self type here, so the
// associated type may carry it.
struct Holder<'a>(&'a u8);
trait HasAssoc {
    type Ty;
}
impl<'a> HasAssoc for Holder<'a> {
    type Ty = &'a u8;
}

fn main() {
    let x: u32 = Carrier::<u32>::get(&1u8);
    assert_eq!(x, 0);
    let y: u32 = Sink::sink(&2u16);
    assert_eq!(y, 0);
    let _: <(u8, u8) as Indirect>::Ty = ();
    let one = 1u8;
    let h = Holder(&one);
    let t: <Holder<'_> as HasAssoc>::Ty = h.0;
    assert_eq!(*t, 1);
}
