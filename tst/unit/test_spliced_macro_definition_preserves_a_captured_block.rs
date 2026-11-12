macro_rules! build {
    (($($prefix:tt)*) $body:block) => {
        $($prefix)* {
            () => {{
                let base = 997u16;
                let result = $body;
                (base, result)
            }};
        }
    };
}

fn main() {
    let base = 773u16;
    build!((macro_rules! evaluate) { base + 8 });
    assert_eq!(evaluate!(), (997, 781));
}
