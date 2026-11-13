// cfg tombstones belong to expanded copies, not to the repeated block template.
macro_rules! twice {
    ($block:block) => { ($block, $block) };
}

fn main() {
    let values = twice!({
        #[cfg(any())]
        fn disabled() -> u32 { 999 }

        trait Local {
            #[cfg(any())]
            fn disabled_associated();
            const VALUE: u32 = 42;
        }

        struct Marker;
        impl Local for Marker {}

        extern "C" {
            #[cfg(any())]
            fn disabled_foreign();
        }

        <Marker as Local>::VALUE
    });
    assert_eq!(values.0, 42);
    assert_eq!(values.1, 42);
}
