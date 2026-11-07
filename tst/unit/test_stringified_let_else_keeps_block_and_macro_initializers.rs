macro_rules! snapshot {
    ($body:block) => {
        (stringify!($body), $body)
    };
}

macro_rules! present {
    () => { Some(719u16) };
}

fn compact(text: &str) -> String {
    text.chars().filter(|ch| !ch.is_whitespace()).collect()
}

fn main() {
    let (block_text, block_value) = snapshot!({
        let Some(value) = ({ Some(613u16) }) else { panic!("missing block value") };
        value + 4
    });
    let (macro_text, macro_value) = snapshot!({
        let Some(value) = present! {} else { panic!("missing macro value") };
        value + 6
    });
    assert_eq!(block_value, 617);
    assert_eq!(macro_value, 725);
    assert!(compact(block_text).contains("=({"));
    assert!(compact(macro_text).contains("else{"));
}
