// toml's `toml!` stringifies `00:32:00.999999` and parses the seconds back.
// Upstream's lexer ends a float literal at the first character that cannot
// continue it; whitespace is such a character and nothing after it is looked
// at. We looked past the whitespace for a `.` and queued the float without
// its spelling, so `00.999999` came back as `0.999999`.
macro_rules! text {
    ($($t:tt)*) => {
        stringify!($($t)*)
    };
}

fn main() {
    assert_eq!(text!(00.999999 x), "00.999999 x");
    assert_eq!(text!(1.50 ..2), "1.50 ..2");
    let r = 1.5 ..2.5;
    assert_eq!(r.start, 1.5);
}
