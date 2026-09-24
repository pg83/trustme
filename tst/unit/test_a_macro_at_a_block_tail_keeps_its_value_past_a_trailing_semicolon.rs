// tokio-test's `assert_ready_err!` ends its block with `assert_err!(val)`,
// and `assert_err!($e)` expands to `assert_err!($e,);`. A parenthesised
// macro call that closes a block is an expression (upstream
// `parse_stmt_mac`: not followed by `;`), expanded as one, and an
// expression's expansion drops a trailing `;`
// (`semicolon_in_expressions_from_macros`), so the block has the value.
#![allow(semicolon_in_expressions_from_macros)]

macro_rules! unwrap_err {
    ($e:expr) => {
        unwrap_err!($e,);
    };
    ($e:expr,) => {{
        match $e {
            Ok(_) => panic!("was ok"),
            Err(e) => e,
        }
    }};
}

macro_rules! first_err {
    ($e:expr) => {{
        let val = $e;
        unwrap_err!(val)
    }};
}

fn main() {
    let err: u8 = first_err!(Err::<(), u8>(5));
    assert_eq!(err, 5);
    let direct = { unwrap_err!(Err::<(), u8>(6)) };
    assert_eq!(direct, 6);
}
