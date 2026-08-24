//@ test-harness

use core::marker::PhantomData;

mod first {
    use super::*;

    mod second {
        use super::*;

        #[test]
        fn nested_private_glob_import() {
            mod local {
                use super::*;

                pub(super) struct Wrapper<T>(pub(super) PhantomData<T>);
            }

            let _ = local::Wrapper::<u8>(PhantomData);
        }
    }
}
