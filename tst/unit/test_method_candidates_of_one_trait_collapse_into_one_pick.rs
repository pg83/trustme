// icu_normalizer: `D: DataProvider<A> + DataProvider<B> + DataProvider<C>`
// and `provider.load(Default::default())?.payload` read into a
// `DataPayload<A>`. Each where-clause is a candidate for `load`; upstream's
// `collapse_candidates_to_trait_pick` turns candidates that all come from
// one trait into one pick of that trait with its parameters left to
// inference, and the annotation then selects `A`.
pub trait Marker { const ID: u16; }
pub struct A;
pub struct B;
impl Marker for A { const ID: u16 = 1; }
impl Marker for B { const ID: u16 = 2; }
pub struct Payload<M: Marker>(core::marker::PhantomData<M>);
impl<M: Marker> Payload<M> { fn id(&self) -> u16 { M::ID } }
pub struct Response<M: Marker> { pub payload: Payload<M> }
#[derive(Default)]
pub struct Request;
pub trait Provider<M: Marker> { fn load(&self, req: Request) -> Result<Response<M>, ()>; }
struct Both;
impl<M: Marker> Provider<M> for Both {
    fn load(&self, _: Request) -> Result<Response<M>, ()> { Ok(Response { payload: Payload(core::marker::PhantomData) }) }
}
fn get<D: Provider<A> + Provider<B> + ?Sized>(d: &D) -> Result<u16, ()> {
    let a: Payload<A> = d.load(Default::default())?.payload;
    let b: Payload<B> = d.load(Default::default())?.payload;
    Ok(a.id() * 10 + b.id())
}
fn main() { assert_eq!(get(&Both), Ok(12)); }
