//@ aux-build: upcast_base.rs
//@ aux-build: upcast_derived.rs
// arti-client (arti) calls `dirmgr.clone().upcast_arc()` on an
// `Arc<dyn DirProvider>`, where `DirProvider: NetDirProvider` and
// `NetDirProvider: UpcastArcNetDirProvider + Send + Sync`, whose
// `upcast_arc(self: Arc<Self>)` is implemented for every sized provider.
// arti-client imports `NetDirProvider` but not `UpcastArcNetDirProvider`.
// Upstream's method probe takes the methods of the object type's principal
// trait and of all its supertraits as inherent candidates
// (`assemble_inherent_candidates_from_object`), so no import is needed.
use std::sync::Arc;
use upcast_derived::{Derived, Provider};

fn main() {
    let derived: Arc<dyn Derived> = Arc::new(Provider);
    assert_eq!(derived.extra(), 5);
    let base = derived.clone().upcast_arc();
    assert_eq!(base.id(), 4);
}
