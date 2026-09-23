// tokio's `cfg_unstable_metrics! { cfg_64bit_metrics! { pub fn .. } }` inside
// an `impl`: the outer macro hands each `$item:item` on, and one of them is
// itself a macro call. Upstream parses the whole item and converts it to an
// associated item (`AssocItemKind::try_from(ItemKind)`), where a macro call
// stays a macro call and is expanded in the impl.
macro_rules! outer {
    ($($item:item)*) => { $( $item )* };
}
macro_rules! inner {
    ($($item:item)*) => { $( $item )* };
}
struct S;
impl S {
    outer! {
        inner! {
            pub fn one(&self) -> u32 { 1 }
        }
        pub fn two(&self) -> u32 { 2 }
    }
}
fn main() {
    assert_eq!(S.one() + S.two(), 3);
}
