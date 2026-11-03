#![feature(pattern_types, pattern_type_macro)]
macro_rules! inspect {
    ($body:block) => {{
        let value = $body;
        let spelling = stringify!($body);
        assert!(spelling.contains("Local"));
        assert!(spelling.contains("Window"));
        value
    }};
}
fn main() {
    let value = inspect!({
        #[derive(Clone, Copy, Debug, PartialEq, Eq)]
        struct Local<T>(T);
        type Window = std::pat::pattern_type!(u16 is 3..=17);
        let ranged: Window = 13;
        let plain = unsafe { std::mem::transmute::<Window, u16>(ranged) };
        let item = Local(plain);
        assert_eq!(item.clone(), item);
        assert_eq!(format!("{:?}", item), "Local(13)");
        item.0
    });
    assert_eq!(value, 13);
}
