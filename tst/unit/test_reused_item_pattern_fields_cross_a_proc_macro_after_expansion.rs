//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
#![feature(pattern_types, pattern_type_macro)]

macro_rules! in_two_modules {
    ($item:item) => {
        mod first { $item }
        mod second {
            #[proc_macro_item_passthrough::echo_item]
            $item
        }
    };
}

in_two_modules! {
    pub struct Window(pub std::pat::pattern_type!(u16 is 3..=17));
}

fn main() {
    let first = first::Window(5);
    let second = second::Window(13);
    assert_eq!(unsafe { std::mem::transmute::<_, u16>(first.0) }, 5);
    assert_eq!(unsafe { std::mem::transmute::<_, u16>(second.0) }, 13);
    assert_eq!(std::mem::size_of::<first::Window>(), 2);
    assert_eq!(std::mem::size_of::<second::Window>(), 2);
}
