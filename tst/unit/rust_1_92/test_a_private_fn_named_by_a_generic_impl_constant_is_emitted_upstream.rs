//@ aux-build: generic_impl_const_fn.rs
// `<ByVal<T> as DerefVTable>::VTABLE` is a constant of a generic trait impl
// whose body names the crate's private `no_deref` and `deref`; it is
// evaluated here and yields pointers to them. rustc's reachability pass walks
// the bodies of impl items that other crates may instantiate, constants as
// well as methods (`ImplItemKind::Const`), so the private functions are
// reachable non-generics the defining crate emits and exports. We walked the
// constants of inherent impls only, so neither function was emitted and the
// link failed: equator's `DerefVTable`, under image.
use generic_impl_const_fn::{ByVal, DerefVTable};

fn main() {
    let value = 5u8;
    let pointer = &value as *const u8 as *const ();
    let slot = &pointer as *const *const ();
    let small = <ByVal<u8> as DerefVTable>::VTABLE;
    assert_eq!(unsafe { small(slot) }, slot as *const ());
    let large = <ByVal<[u64; 4]> as DerefVTable>::VTABLE;
    assert_eq!(unsafe { large(slot) }, pointer);
}
