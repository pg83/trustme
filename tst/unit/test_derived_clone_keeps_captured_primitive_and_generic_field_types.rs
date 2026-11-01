macro_rules! define {
    ($primitive:ty, $generic:ident) => {
        #[derive(LateClone)]
        struct Bundle<$generic> { scalar: $primitive, payload: $generic }
    };
}
macro_rules! install { () => { use core::clone::Clone as LateClone; }; }
define!(u16, T);
install!();
fn main() {
    let original = Bundle { scalar: 17, payload: String::from("field") };
    let cloned = original.clone();
    assert_eq!((cloned.scalar, cloned.payload.as_str()), (17, "field"));
    assert_eq!(original.payload, "field");
}
