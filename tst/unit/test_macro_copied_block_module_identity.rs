use std::sync::atomic::Ordering;

macro_rules! relay {
    ($item:item) => { $item };
}

macro_rules! duplicate {
    ($item:item) => {
        mod first { relay! { $item } }
        macro_rules! later {
            () => { mod second { relay! { $item } } };
        }
        later!();
    };
}

duplicate! {
    pub mod copied {
        pub fn next() -> usize {
            mod state {
                use std::sync::atomic::AtomicUsize;
                pub static COUNT: AtomicUsize = AtomicUsize::new(0);
            }
            state::COUNT.fetch_add(1, crate::Ordering::SeqCst)
        }

        pub fn nested() -> usize {
            mod outer {
                pub fn value() -> usize {
                    mod inner { pub const VALUE: usize = 37; }
                    inner::VALUE
                }
            }
            let indirect = || {
                mod closure_local { pub const EXTRA: usize = 5; }
                closure_local::EXTRA
            };
            outer::value() + indirect()
        }
    }
}

fn main() {
    assert_eq!(first::copied::next(), 0);
    assert_eq!(second::copied::next(), 0);
    assert_eq!(first::copied::next(), 1);
    assert_eq!(second::copied::next(), 1);
    assert_eq!(first::copied::nested(), 42);
    assert_eq!(second::copied::nested(), 42);
}
