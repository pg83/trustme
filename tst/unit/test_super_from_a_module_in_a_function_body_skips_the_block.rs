// generic-tests (prefix-trie's dev-dependency) instantiates a generic test as
// `fn name() { mod shim { fn shim() { super::super::name::<T>() } } .. }`:
// the module sits in the body of a function in the instantiating module.
// Upstream resolves `super` from the parent's nearest normal module
// (`resolve_self`, rustc_resolve/src/ident.rs): a function body's block is
// no module, so `super` is the module the function is in and `super::super`
// the generic test's own module - not the instantiated test function of the
// same name.
mod outer {
    pub fn target() -> u32 {
        1
    }

    pub mod inner {
        pub fn target() -> u32 {
            2
        }

        pub fn run() -> u32 {
            mod shim {
                pub(super) fn call() -> u32 {
                    super::super::target()
                }

                pub(super) fn here() -> u32 {
                    super::target()
                }
            }
            shim::call() * 10 + shim::here()
        }
    }
}

fn main() {
    assert_eq!(outer::inner::run(), 12);
}
