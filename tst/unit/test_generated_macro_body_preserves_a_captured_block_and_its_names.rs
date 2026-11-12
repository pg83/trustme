macro_rules! surround {
    ($body:block) => {{
        macro_rules! generated {
            () => {{
                let base = 991u16;
                let result = $body;
                (base, result)
            }};
        }
        generated!()
    }};
}

fn main() {
    let base = 769u16;
    assert_eq!(surround!({ base + 4 }), (991, 773));
}
