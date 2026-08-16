// A macro fragment stands where its kind is written, and three positions did
// not accept one: a `meta` fragment inside `#[derive(...)]`, a `ty` fragment
// inside `#[repr(...)]`, and a `block` fragment after `const`.
//
// Same shape as the upstream tests attributes/issue-40962.rs,
// attributes/decl_macro_ty_in_attr_macro.rs and inline-const/interpolated.rs.
#![allow(dead_code)]

macro_rules! derived {
    ($i:meta) => {
        #[derive($i)]
        struct S;
    };
}

derived!(Clone);

macro_rules! reprAs {
    ($repr:ty) => {
        #[repr($repr)]
        pub enum Foo {
            Bar = 0i32,
        }
    };
}

reprAs! { i32 }

macro_rules! constBlock {
    ($b:block) => {
        fn five() -> u32 {
            const $b
        }
        fn six() -> u32 {
            let v = const $b;
            v + 1
        }
    };
}

constBlock!({ 5 });

fn main() {
    let s = S;
    let _ = s.clone();
    assert_eq!(Foo::Bar as i32, 0);
    assert_eq!(std::mem::size_of::<Foo>(), 4);
    assert_eq!(five(), 5);
    assert_eq!(six(), 6);
}
