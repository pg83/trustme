
struct Pattern(i32);

fn pattern_as_arg(Pattern(value): Pattern) -> i32 {
    value
}

fn gccrs_main() -> i32 {
    pattern_as_arg(Pattern(15)) - 15
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
