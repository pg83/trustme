
macro_rules! matches {
    ($expression:expr, $($pattern:pat)|+ $( if $guard:expr ),*) => {
        match $expression {
            $($pattern)|+ => true,
            _ => false,
        }
    }
}

pub fn should_match() -> bool {
    matches!(1, 1)
}

pub fn shouldnt() -> bool {
    matches!(1, 2)
}

fn gccrs_main() -> i32 {
    let mut retval = 2;

    if should_match() {
        retval -= 1;
    }

    if !shouldnt() {
        retval -= 1;
    }

    retval
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
