//@ aux-build: opaque_auto_core.rs
// tokio's async_send_sync test asserts which of its futures are `Send`,
// `Sync` and `Unpin` through `AmbiguousIf*` traits: a method call that has
// one impl when the auto trait is missing and two when it holds. The futures
// are opaque types of tokio's `async fn`s. Upstream an auto trait of an
// opaque type holds as it holds of the hidden type
// (`instantiate_constituent_tys_for_auto_trait`); another crate's hidden type
// is what its metadata recorded. Every auto trait was taken to hold of an
// opaque type, so `Receiver::ready`'s future came out `Unpin`.
extern crate opaque_auto_core;

use opaque_auto_core::{local_count, ready_now, Receiver};

trait AmbiguousIfUnpin<A> {
    fn unpin_probe(&self) {}
}
impl<T: ?Sized> AmbiguousIfUnpin<()> for T {}
impl<T: ?Sized + Unpin> AmbiguousIfUnpin<u8> for T {}

trait AmbiguousIfSend<A> {
    fn send_probe(&self) {}
}
impl<T: ?Sized> AmbiguousIfSend<()> for T {}
impl<T: ?Sized + Send> AmbiguousIfSend<u8> for T {}

fn require_unpin<T: Unpin>(_: &T) {}
fn require_send<T: Send>(_: &T) {}

fn main() {
    let receiver = Receiver(3);
    let ready = receiver.ready();
    ready.unpin_probe();
    require_send(&ready);

    let now = ready_now(4);
    require_unpin(&now);
    require_send(&now);

    let counted = local_count();
    counted.send_probe();
    counted.unpin_probe();
}
