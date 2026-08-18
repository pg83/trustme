// A `macro_rules!` written in a block is in scope for the items of that block,
// including a trait or impl body, even though those items are expanded first.
fn main() {
    macro_rules! const_maker {
        ($t:ty, $v:tt) => { const CONST: $t = $v; };
    }
    macro_rules! method_maker {
        ($n:ident, $v:expr) => { fn $n(&self) -> u32 { $v } };
    }

    trait T {
        const_maker! { i32, 7 }
    }

    struct S;
    impl T for S {}
    impl S {
        method_maker! { get, 11 }
    }

    assert_eq!(<S as T>::CONST, 7);
    assert_eq!(S.get(), 11);
}
