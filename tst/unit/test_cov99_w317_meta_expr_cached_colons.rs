macro_rules! capture_tail {
    ($meta:meta $separator:tt $tail:literal) => {
        $tail
    };
}

fn main() {
    let tail = capture_tail!(tag = 1 :: "cached colons");
    assert_eq!(tail, "cached colons");
}
