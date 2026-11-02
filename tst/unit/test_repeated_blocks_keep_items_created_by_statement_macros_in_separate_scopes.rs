//@ compile-variant: -O
macro_rules! local_type { () => { struct Local; }; }
macro_rules! pair { ($body:expr) => {{ let first = $body; let second = $body; (first, second) }}; }
fn main() {
    let (first, second) = pair!({
        const VALUE: u8 = 7;
        local_type!();
        assert_eq!(VALUE, 7);
        std::any::TypeId::of::<Local>()
    });
    assert_ne!(first, second);
}
