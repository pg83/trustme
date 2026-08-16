// `auto trait` is an item like any other, so it can be declared inside a
// function body. The statement parser only knew the keyword-led item starts, so
// `auto` there was read as the start of an expression and the following `trait`
// was an unexpected token.
//
// Same shape as the upstream test auto-traits/auto-traits.rs.
#![feature(auto_traits)]
#![feature(negative_impls)]

fn main() {
    auto trait Inner {}

    unsafe auto trait InnerUnsafe {}

    struct S;
    impl !Inner for S {}

    fn takesInner<T: Inner>(_: T) -> u32 {
        1
    }
    fn takesInnerUnsafe<T: InnerUnsafe>(_: T) -> u32 {
        2
    }

    // An auto trait holds for anything that has not opted out.
    assert_eq!(takesInner(1u32), 1);
    assert_eq!(takesInnerUnsafe(S), 2);
}
