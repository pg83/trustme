//@ compile-only
//@ compile-flags: --emit=metadata
//@ compile-fail: Invalid read:

union EmptyOrBool {
    empty: (),
    flag: bool,
}

const BAD: bool = {
    let value = EmptyOrBool { empty: () };
    unsafe { value.flag & true }
};

fn main() {}
