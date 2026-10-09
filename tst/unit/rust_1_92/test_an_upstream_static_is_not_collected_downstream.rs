//@ aux-build: dyn_fn_static.rs
// `Suite::<1>::kem` is instantiated here and reads the upstream static `KEM`,
// whose value holds `&generate_secret` as a `&dyn Fn() -> u8`. rustc's
// collector does not collect an upstream non-generic static
// (`should_codegen_locally` is false for it): the crate that defines it emits
// its allocation, so the vtable and the private function it points to are
// never instantiated here. rustls' HPKE suites are such statics, and the
// compiler aborted trying to make the vtable of a private fn item it has no
// signature for.
use dyn_fn_static::Suite;

fn main() {
    assert_eq!((Suite::<1>.kem().generate)(), 7);
}
