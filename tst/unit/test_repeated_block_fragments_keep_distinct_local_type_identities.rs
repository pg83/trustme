//@ compile-variant: -O
macro_rules! identities { ($body:expr) => {{ let first = $body; let second = $body; (first, second) }}; }
fn main() {
    let (first, second) = identities!({
        #[derive(Clone)]
        struct Local;
        std::any::TypeId::of::<Local>()
    });
    assert_ne!(first, second);
}
