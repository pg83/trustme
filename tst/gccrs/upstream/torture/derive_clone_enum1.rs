#[derive(Clone)]
enum MixAndMatch {
    A,
    B(i32),
    C { inner: i32 },
}

fn gccrs_main() -> i32 {
    let a = MixAndMatch::A;
    let a_copy = a.clone();

    // we want res to stay at zero - when we don't match on the right thing, increase it

    let mut res = match a_copy {
        MixAndMatch::A => 0,
        _ => 1,
    };

    let a = MixAndMatch::B(15);
    let a_copy = a.clone();

    match a_copy {
        MixAndMatch::B(15) => {}
        _ => res += 1,
    };

    let a = MixAndMatch::C { inner: 15 };
    let a_copy = a.clone();

    match a_copy {
        MixAndMatch::C { inner } => {
            if inner != 15 {
                res += 1;
            }
        }
        _ => res += 1,
    };

    res
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
