//@ compile-fail: private
macro_rules! duplicate {
    ($item:item) => { mod left { $item } mod right { $item } };
}
duplicate!(mod scope {
    pub fn identities() -> (std::any::TypeId, std::any::TypeId) {
        macro_rules! pair { ($body:expr) => {{ let a = $body; let b = $body; (a, b) }}; }
        pair!({ struct Local; std::any::TypeId::of::<Local>() })
    }
});
fn main() {
    let (a, b) = left::scope::identities();
    let (c, d) = right::scope::identities();
    assert_ne!(a, b);
    assert_ne!(c, d);
    assert_ne!(a, c);
    assert_ne!(b, d);
}
