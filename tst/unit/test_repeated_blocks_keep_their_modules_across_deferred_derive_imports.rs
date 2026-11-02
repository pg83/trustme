//@ proc-macro-aux-build: inspect_type_expansion_order.rs
//@ compile-variant: -O
macro_rules! install { () => { use inspect_type_expansion_order::InspectTypes; }; }
macro_rules! pair { ($body:expr) => {{ let first = $body; let second = $body; (first, second) }}; }
fn main() {
    let values = pair!({
        #[derive(InspectTypes)]
        struct Local(u8);
        install!();
        let value = Local(17);
        !OBSERVED_SELF && !OBSERVED_PATTERN && value.0 == 17
    });
    assert_eq!(values, (true, true));
}
