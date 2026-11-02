//@ compile-fail: private
macro_rules! duplicate {
    ($item:item) => { mod left { $item } mod right { $item } };
}
duplicate!(mod scope {
    pub fn identity() -> std::any::TypeId {
        struct Local;
        std::any::TypeId::of::<Local>()
    }
});
fn main() { assert_ne!(left::scope::identity(), right::scope::identity()); }
