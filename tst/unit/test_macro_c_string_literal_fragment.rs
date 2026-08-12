macro_rules! literal {
    ($value:literal) => {
        $value
    };
}

macro_rules! expression {
    ($value:expr) => {
        $value
    };
}

fn main() {
    let literal = literal!(c"\xEF\x80🦀\u{1F980}").to_bytes();
    let expression = expression!(c"expression").to_bytes();
    if literal.len() != 10
        || literal[0] != 0xEF
        || literal[1] != 0x80
        || literal[2] != 0xF0
        || literal[6] != 0xF0
        || expression.len() != 10
        || expression[0] != b'e'
    {
        std::process::abort();
    }
}
