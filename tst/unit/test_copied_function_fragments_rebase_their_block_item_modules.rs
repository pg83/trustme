//@ compile-variant: -O
macro_rules! duplicate {
    ($item:item) => { mod left { $item } mod right { $item } };
}
duplicate!(pub fn identity() -> std::any::TypeId {
    type Number = u32;
    struct Local(Number);
    let value = Local(17);
    assert_eq!(value.0, 17);
    std::any::TypeId::of::<Local>()
});
fn main() { assert_ne!(left::identity(), right::identity()); }
