macro_rules! stringify_block {
    ($block:block) => {
        stringify!($block)
    };
}

fn main() {
    let rendered = stringify_block!({
        let value = 1;
        value + 2
    });

    assert!(rendered.starts_with('{'));
    assert!(rendered.contains("value"));
    assert!(rendered.contains('+'));
    assert!(rendered.ends_with('}'));
}
