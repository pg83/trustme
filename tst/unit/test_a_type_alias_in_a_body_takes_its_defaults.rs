// futures' auto_traits writes `assert_not_impl!(Then<UnpinFuture, ..>: Unpin)`
// with `type UnpinFuture<T = PhantomPinned> = LocalFuture<T>` and
// `type LocalFuture<T = *const ()> = Pin<Box<dyn Future<Output = T>>>`.
// Only a segment of a value path infers its omitted arguments; a type
// written in a body, a let annotation or the self type of a qualified
// path, takes the alias's defaults.
#[derive(Default)]
struct W<T = u8>(T);
type LW<T = u8> = W<T>;
type Local<T = u8> = Box<T>;

fn main() {
    let e: LW = Default::default();
    assert_eq!(std::mem::size_of_val(&e), 1);
    let f = <LW>::default();
    assert_eq!(std::mem::size_of_val(&f), 1);
    let g = <(Local, u16) as Default>::default();
    assert_eq!(std::mem::size_of_val(&*g.0), 1);
    let h = LW::default();
    let _: W<u16> = h;
}
