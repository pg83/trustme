macro_rules! imports {
    () => {
        #[cfg(any())]
        extern crate unavailable_crate;
        #[cfg(all())]
        extern crate core as generated_core;
    };
}
imports!();

mod nested {
    imports!();
    pub fn width() -> usize { generated_core::mem::size_of::<u32>() }
}

fn main() {
    assert_eq!(generated_core::mem::size_of::<u16>(), 2);
    assert_eq!(nested::width(), 4);
}
