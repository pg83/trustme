// A parameter may carry attributes, wherever parameters are written. They say
// nothing about the parameter except that a failing `#[cfg]` removes it, arity
// and all.
#![allow(dead_code)]

type Callback = fn(#[allow(unused)] a: i32, #[cfg(FALSE)] gone: i32, #[deny(unused)] b: i32);

struct Counter(u32);

impl Counter {
    fn get(#[allow(unused)] &self, #[cfg(FALSE)] gone: i32, #[forbid(unused)] extra: u32) -> u32 {
        self.0 + extra
    }
}

extern "C" {
    fn printf(#[allow(unused)] fmt: *const i8, #[deny(unused)] #[warn(unused)] ...);
}

fn plain(#[allow(unused)] a: u32, #[cfg(FALSE)] gone: u32, #[warn(unused)] b: u32) -> u32 {
    a + b
}

// A failing `#[cfg]` on the *first* parameter removes it too, in every spelling.
fn first(#[cfg(false)] gone: u32, #[cfg(any())] also_gone: u32, #[cfg(not(all()))] third: u32, kept: u32) -> u32 {
    kept
}

fn main() {
    assert_eq!(plain(1, 2), 3);
    assert_eq!(first(7), 7);
    assert_eq!(Counter(1).get(2), 3);

    let closure = |#[allow(unused)] a: u32, #[cfg(FALSE)] gone: u32, #[deny(unused)] b: u32| a + b;
    assert_eq!(closure(4, 5), 9);

    let _: Callback = |_, _| {};
}
